#include "AudioSystem.hpp"

#include "module/AudioClipCache.hpp"
#include "module/AudioSpatial.hpp"
#include "module/AudioValidation.hpp"
#include "module/VoicePool.hpp"
#include "PhysicsSystem.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace sat {
namespace {
template<typename Vector>
bool finite(const Vector& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

struct AudioSystem::Impl {
    Impl(Scene& scene, AudioBackend backend) : m_scene(scene), m_backend(backend) {}
    ~Impl() { stop(); }

    bool fail(std::string message) {
        m_error = std::move(message);
        return false;
    }

    bool check(ma_result result, std::string_view operation) {
        return result == MA_SUCCESS || fail(std::string(operation) + ": " + ma_result_description(result));
    }

    bool requireRunning() {
        m_error.clear();
        return m_running || fail("audio is not running");
    }

    Voice* activeVoice(EntityId owner) {
        if (!requireRunning()) {
            return nullptr;
        }
        Voice* voice = m_voices.find(owner);
        if (!voice || !m_scene.entityById(owner) || ma_sound_at_end(&voice->sound)) {
            fail("Entity has no active audio voice");
            return nullptr;
        }
        return voice;
    }

    bool ownersMayBeDestroyed() {
        const auto revision = m_scene.destructionRevision();
        return std::exchange(m_destructionRevision, revision) != revision;
    }

    void retireVoices() {
        const bool destroyed = ownersMayBeDestroyed();
        for (auto& voice : m_voices.slots()) {
            if (voice.initialized && (ma_sound_at_end(&voice.sound) ||
                (destroyed && voice.owner != kInvalidEntityId && !m_scene.entityById(voice.owner)))) {
                m_voices.release(voice);
            }
        }
    }

    static DVec3 velocity(const PhysicsSystem* physics, EntityId id, DVec3 current,
        DVec3 previous, double inverseDelta, float timeScale) {
        if (physics && id != kInvalidEntityId) {
            if (const auto physical = physics->linearVelocity(id)) {
                return *physical * timeScale;
            }
        }
        return inverseDelta > 0.0 ? (current - previous) * inverseDelta : DVec3{};
    }

    float playbackScale(const Voice& voice) const {
        return m_followTimeScale[static_cast<std::size_t>(voice.bus)] ? m_timeScale : 1.0f;
    }

    void syncPlayback(Voice& voice) {
        const float scale = playbackScale(voice);
        const bool mix = !voice.paused && !voice.virtualized && scale > 0.0f;
        if (scale > 0.0f) {
            const float pitch = m_followTimeScale[static_cast<std::size_t>(voice.bus)]
                ? static_cast<float>(std::clamp(static_cast<double>(voice.pitch) * scale, 0.01, 16.0))
                : voice.pitch;
            if (pitch != voice.appliedPitch) {
                ma_sound_set_pitch(&voice.sound, pitch);
                voice.appliedPitch = pitch;
            }
        }
        if (mix != voice.mixing) {
            if (mix) {
                ma_sound_start(&voice.sound);
            } else {
                ma_sound_stop(&voice.sound);
            }
            voice.mixing = mix;
        }
    }

    void rebalance() {
        m_ranked.clear();
        for (auto& voice : m_voices.slots()) {
            if (voice.initialized) { m_ranked.push_back(&voice); }
        }
        if (m_ranked.size() > m_maxMixing) {
            std::sort(m_ranked.begin(), m_ranked.end(), [this](const Voice* a, const Voice* b) {
                const bool activeA = !a->paused && playbackScale(*a) > 0.0f;
                const bool activeB = !b->paused && playbackScale(*b) > 0.0f;
                if (activeA != activeB) { return activeA; }
                if (a->priority != b->priority) { return a->priority > b->priority; }
                return a->sequence < b->sequence;
            });
        }
        // Suspend outgoing nodes before activating the incoming ones.
        for (std::size_t index = m_maxMixing; index < m_ranked.size(); ++index) {
            auto& voice = *m_ranked[index];
            voice.virtualized = true;
            syncPlayback(voice);
        }
        for (std::size_t index = 0; index < std::min(m_maxMixing, m_ranked.size()); ++index) {
            auto& voice = *m_ranked[index];
            voice.virtualized = false;
            syncPlayback(voice);
        }
    }

    void spatialize(Voice& voice, DVec3 worldPosition, Vec3 rotation, const Entity* listener,
        const AudioBasis& basis, DVec3 listenerVelocity, double inverseDelta, const PhysicsSystem* physics) {
        if (!voice.spatial) {
            return;
        }

        if (listener) {
            const DVec3 relative = worldPosition - listener->transform.position;
            const Vec3 position = basis.toMixer(relative);
            if (finite(position)) {
                Vec3 velocity{};
                if (voice.dopplerSpeedLimit > 0.0) {
                    const double rate = voice.previousListener == listener->id() ? inverseDelta : 0.0;
                    const DVec3 relativeVelocity = Impl::velocity(physics, voice.owner, worldPosition,
                        voice.previousPosition, rate, m_timeScale) - listenerVelocity;
                    const double speed = std::hypot(relativeVelocity.x, relativeVelocity.y, relativeVelocity.z);
                    if (std::isfinite(speed) && speed > 0.0) {
                        velocity = basis.toMixer(relativeVelocity * std::min(1.0, voice.dopplerSpeedLimit / speed));
                    }
                }
                const Vec3 forward = finite(rotation)
                    ? transformDirection(rotationMatrixEuler(rotation), {0.0f, 0.0f, 1.0f})
                    : Vec3{0.0f, 0.0f, 1.0f};
                const Vec3 direction = basis.toMixer({forward.x, forward.y, forward.z});
                ma_sound_set_position(&voice.sound, position.x, position.y, position.z);
                ma_sound_set_direction(&voice.sound, direction.x, direction.y, direction.z);
                ma_sound_set_velocity(&voice.sound, velocity.x, velocity.y, velocity.z);
                if (voice.previousListener == kInvalidEntityId) {
                    ma_sound_set_volume(&voice.sound, voice.volume);
                }
                voice.previousPosition = worldPosition;
                voice.previousListener = listener->id();
                return;
            }
        }
        ma_sound_set_volume(&voice.sound, 0.0f);
        voice.previousListener = kInvalidEntityId;
    }

    bool play(EntityId owner, std::string_view path, const AudioSourceComponent& source, DVec3 position, Vec3 rotation) {
        Voice* previous = m_voices.find(owner);
        if (!previous && m_voices.full()) {
            retireVoices();
            if (m_voices.full()) {
                return fail("Audio voice limit reached");
            }
        }

        ma_uint32 flags = 0;
        auto& manager = *ma_engine_get_resource_manager(&m_engine);
        const auto* clip = m_clips.load(manager, m_reader, path, flags, m_error);
        if (!clip) {
            return false;
        }
        if (previous) {
            m_voices.release(*previous);
        }

        Voice* voice = m_voices.acquire(owner);
        if (!voice) {
            return fail("Audio voice limit reached");
        }
        if (!source.spatial) {
            flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
        }
        if (source.loop) {
            flags |= MA_SOUND_FLAG_LOOPING;
        }
        const auto result = ma_sound_init_from_file(&m_engine, clip->c_str(), flags,
            &m_buses[static_cast<std::size_t>(source.bus)], nullptr, &voice->sound);
        if (result != MA_SUCCESS) {
            m_voices.release(*voice);
            rebalance();
            return fail("Cannot decode audio " + *clip + ": " + ma_result_description(result));
        }

        voice->initialized = true;
        voice->spatial = source.spatial;
        voice->position = position;
        voice->volume = source.volume;
        voice->pitch = source.pitch;
        voice->priority = source.priority;
        voice->bus = source.bus;
        voice->sequence = m_nextSequence++;
        ma_sound_set_volume(&voice->sound, source.volume);
        if (source.spatial) {
            ma_sound_set_min_distance(&voice->sound, source.minDistance);
            ma_sound_set_max_distance(&voice->sound, source.maxDistance);
            ma_sound_set_rolloff(&voice->sound, source.rolloff);
            ma_sound_set_doppler_factor(&voice->sound, source.dopplerFactor);
            voice->dopplerSpeedLimit = source.dopplerFactor > 0.0f
                ? std::min(0.9 * m_speedOfSound / source.dopplerFactor,
                    static_cast<double>(std::numeric_limits<float>::max()))
                : 0.0;
            ma_sound_set_positioning(&voice->sound, ma_positioning_relative);
            ma_sound_set_pinned_listener_index(&voice->sound, 0);
            const auto* listener = audioListener(m_scene);
            if (listener && !finite(listener->transform.rotation)) {
                listener = nullptr;
            }
            const auto basis = listener ? audioBasis(listener->transform.rotation) : AudioBasis{};
            spatialize(*voice, position, rotation, listener, basis, {}, 0.0, nullptr);
        }
        rebalance();
        return true;
    }

    void stop() {
        m_running = false;
        if (m_engineInitialized) {
            ma_engine_stop(&m_engine);
            m_voices.clear();
            while (m_busCount > 0) {
                ma_sound_group_uninit(&m_buses[--m_busCount]);
            }
            m_clips.clear(*ma_engine_get_resource_manager(&m_engine));
            ma_engine_uninit(&m_engine);
            m_engineInitialized = false;
        }
        if (m_contextInitialized) {
            ma_context_uninit(&m_context);
            m_contextInitialized = false;
        }
    }

    Scene& m_scene;
    AudioBackend m_backend;
    AssetReader m_reader;
    std::string m_error;
    AudioClipCache m_clips;
    VoicePool m_voices;
    ma_context m_context{};
    ma_engine m_engine{};
    std::array<ma_sound_group, kAudioBusCount> m_buses{};
    std::size_t m_busCount = 0;
    std::vector<Voice*> m_ranked;
    std::size_t m_maxMixing = 0;
    std::uint64_t m_nextSequence = 0;
    bool m_followTimeScale[kAudioBusCount] = {};
    float m_timeScale = 1.0f;
    EntityId m_previousListener = kInvalidEntityId;
    DVec3 m_previousListenerPosition{};
    float m_speedOfSound = 343.0f;
    std::uint64_t m_destructionRevision = 0;
    bool m_contextInitialized = false;
    bool m_engineInitialized = false;
    bool m_running = false;
};

AudioSystem::AudioSystem(Scene& scene, AudioBackend backend)
    : m_impl(std::make_unique<Impl>(scene, backend)) {}

AudioSystem::~AudioSystem() = default;

void AudioSystem::setAssetReader(AssetReader reader) {
    stop();
    m_impl->m_reader = std::move(reader);
}

bool AudioSystem::start() {
    auto& impl = *m_impl;
    impl.m_error.clear();
    if (impl.m_running) {
        return true;
    }
    const auto& settings = impl.m_scene.audioSettings;
    if (!validAudioSettings(settings)) {
        return impl.fail("Invalid scene audio settings");
    }

    try {
        impl.m_maxMixing = settings.maxVoices;
        const auto capacity = settings.maxVoices + settings.maxVirtualVoices;
        impl.m_voices.reset(capacity);
        impl.m_ranked.reserve(capacity);
        std::copy_n(settings.followTimeScale, kAudioBusCount, impl.m_followTimeScale);
        impl.m_nextSequence = 0;
        impl.m_timeScale = 1.0f;
        impl.m_previousListener = kInvalidEntityId;
        auto config = ma_engine_config_init();
        config.noAutoStart = MA_TRUE;
        config.listenerCount = 1;
        config.pResourceManagerVFS = impl.m_clips.vfs();
        if (impl.m_backend == AudioBackend::Null) {
            const ma_backend backend = ma_backend_null;
            if (!impl.check(ma_context_init(&backend, 1, nullptr, &impl.m_context), "Cannot initialize null audio backend")) {
                return false;
            }
            impl.m_contextInitialized = true;
            config.pContext = &impl.m_context;
        }
        if (!impl.check(ma_engine_init(&config, &impl.m_engine), "Cannot initialize audio engine")) {
            stop();
            return false;
        }
        impl.m_engineInitialized = true;
        // The mixer listener uses a fixed basis. The engine thread supplies offsets in that basis.
        impl.m_speedOfSound = settings.speedOfSound;
        ma_spatializer_listener_set_speed_of_sound(&impl.m_engine.listeners[0], impl.m_speedOfSound);
        if (!impl.check(ma_engine_set_volume(&impl.m_engine, settings.masterVolume), "Cannot set master volume")) {
            stop();
            return false;
        }
        for (auto& bus : impl.m_buses) {
            if (!impl.check(ma_sound_group_init(&impl.m_engine, MA_SOUND_FLAG_NO_PITCH, nullptr, &bus), "Cannot initialize audio bus")) {
                stop();
                return false;
            }
            ma_sound_group_set_volume(&bus, settings.busVolumes[impl.m_busCount++]);
        }

        impl.m_destructionRevision = impl.m_scene.destructionRevision();
        impl.m_running = true;
        for (const auto& entity : impl.m_scene.entities()) {
            if (entity.hasAudioSource && entity.audioSource.playOnStart && !entity.audioSource.clipPath.empty() && !play(entity.id())) {
                stop();
                return false;
            }
        }
        if (!impl.check(ma_engine_start(&impl.m_engine), "Cannot start audio device")) {
            stop();
            return false;
        }
        return true;
    } catch (const std::bad_alloc&) {
        stop();
        return impl.fail("Cannot allocate audio session resources");
    } catch (...) {
        stop();
        throw;
    }
}

void AudioSystem::stop() {
    m_impl->stop();
}

bool AudioSystem::running() const {
    return m_impl->m_running;
}

const std::string& AudioSystem::error() const {
    return m_impl->m_error;
}

void AudioSystem::update(float deltaSeconds, const PhysicsSystem* physics, float timeScale) {
    auto& impl = *m_impl;
    if (!impl.m_running) {
        return;
    }
    if (physics && !physics->running()) { physics = nullptr; }
    const float scale = std::isfinite(timeScale) && timeScale >= 0.0f ? timeScale : 1.0f;
    bool refreshPlayback = scale != impl.m_timeScale;
    impl.m_timeScale = scale;
    const double inverseDelta = std::isfinite(deltaSeconds) && deltaSeconds > 0.0f ? 1.0 / deltaSeconds : 0.0;
    const bool destroyed = impl.ownersMayBeDestroyed();
    const Entity* listener = nullptr;
    AudioBasis basis{};
    DVec3 listenerVelocity{};
    bool listenerSelected = false;
    for (auto& voice : impl.m_voices.slots()) {
        if (!voice.initialized) {
            continue;
        }
        if (ma_sound_at_end(&voice.sound)) {
            impl.m_voices.release(voice);
            refreshPlayback = true;
            continue;
        }
        if (voice.spatial && !listenerSelected) {
            listenerSelected = true;
            listener = audioListener(impl.m_scene);
            if (listener && finite(listener->transform.rotation)) {
                basis = audioBasis(listener->transform.rotation);
                const auto position = listener->transform.position;
                const double rate = listener->id() == impl.m_previousListener ? inverseDelta : 0.0;
                listenerVelocity = Impl::velocity(physics, listener->id(), position,
                    impl.m_previousListenerPosition, rate, impl.m_timeScale);
                impl.m_previousListenerPosition = position;
            } else {
                listener = nullptr;
            }
        }
        if (voice.owner == kInvalidEntityId) {
            impl.spatialize(voice, voice.position, {}, listener, basis, listenerVelocity, inverseDelta, physics);
        } else if (voice.spatial || destroyed) {
            const auto* owner = impl.m_scene.entityById(voice.owner);
            if (owner) {
                impl.spatialize(voice, owner->transform.position, owner->transform.rotation, listener, basis,
                    listenerVelocity, inverseDelta, physics);
            } else {
                impl.m_voices.release(voice);
                refreshPlayback = true;
            }
        }
    }
    impl.m_previousListener = listener ? listener->id() : kInvalidEntityId;
    if (refreshPlayback) { impl.rebalance(); }
}

bool AudioSystem::play(EntityId entity) {
    auto& impl = *m_impl;
    if (!impl.requireRunning()) {
        return false;
    }
    const auto* owner = impl.m_scene.entityById(entity);
    if (!owner || !owner->hasAudioSource) {
        return impl.fail("Entity has no audio source");
    }
    if (!validAudioSource(owner->audioSource)) {
        return impl.fail("Invalid audio source settings on entity " + owner->name);
    }
    return impl.play(entity, owner->audioSource.clipPath, owner->audioSource,
        owner->transform.position, owner->transform.rotation);
}

bool AudioSystem::stopSound(EntityId entity) {
    auto* voice = m_impl->activeVoice(entity);
    if (!voice) {
        return false;
    }
    m_impl->m_voices.release(*voice);
    m_impl->rebalance();
    return true;
}

bool AudioSystem::pause(EntityId entity) {
    auto* voice = m_impl->activeVoice(entity);
    if (!voice) {
        return false;
    }
    voice->paused = true;
    m_impl->rebalance();
    return true;
}

bool AudioSystem::resume(EntityId entity) {
    auto* voice = m_impl->activeVoice(entity);
    if (!voice) {
        return false;
    }
    voice->paused = false;
    m_impl->rebalance();
    return true;
}

bool AudioSystem::isVirtualized(EntityId entity) const {
    const auto* voice = m_impl->m_voices.find(entity);
    return voice && voice->virtualized && m_impl->m_scene.entityById(entity) && !ma_sound_at_end(&voice->sound);
}

bool AudioSystem::isPlaying(EntityId entity) const {
    const Impl& impl = *m_impl;
    const auto* voice = impl.m_voices.find(entity);
    return impl.m_running && voice && !voice->paused && impl.m_scene.entityById(entity) &&
        !ma_sound_at_end(&voice->sound);
}

bool AudioSystem::setVolume(EntityId entity, float volume) {
    auto* voice = m_impl->activeVoice(entity);
    if (!voice) {
        return false;
    }
    if (!validAudioGain(volume)) {
        return m_impl->fail("Audio volume must be finite and nonnegative");
    }
    voice->volume = volume;
    if (!voice->spatial || voice->previousListener != kInvalidEntityId) {
        ma_sound_set_volume(&voice->sound, volume);
    }
    return true;
}

bool AudioSystem::setPitch(EntityId entity, float pitch) {
    auto* voice = m_impl->activeVoice(entity);
    if (!voice) {
        return false;
    }
    if (!validAudioPitch(pitch)) {
        return m_impl->fail("Audio pitch must be finite and positive");
    }
    voice->pitch = pitch;
    m_impl->syncPlayback(*voice);
    return true;
}

bool AudioSystem::playOneShot(std::string_view path, DVec3 position, float volume) {
    auto& impl = *m_impl;
    if (!impl.requireRunning()) {
        return false;
    }
    if (!validAudioGain(volume) || !finite(position)) {
        return impl.fail("One-shot audio requires a finite position and finite nonnegative volume");
    }
    AudioSourceComponent source;
    source.volume = volume;
    return impl.play(kInvalidEntityId, path, source, position, {});
}

bool AudioSystem::setBusVolume(AudioBus bus, float volume) {
    auto& impl = *m_impl;
    if (!impl.requireRunning()) {
        return false;
    }
    if (!validAudioBus(bus) || !validAudioGain(volume)) {
        return impl.fail("Audio bus volume requires a valid bus and finite nonnegative volume");
    }
    ma_sound_group_set_volume(&impl.m_buses[static_cast<std::size_t>(bus)], volume);
    return true;
}

void AudioSystem::setMasterVolume(float volume) {
    auto& impl = *m_impl;
    if (!impl.requireRunning()) {
        return;
    }
    if (!validAudioGain(volume)) {
        impl.fail("Audio master volume must be finite and nonnegative");
        return;
    }
    impl.check(ma_engine_set_volume(&impl.m_engine, volume), "Cannot set master volume");
}

} // namespace sat
