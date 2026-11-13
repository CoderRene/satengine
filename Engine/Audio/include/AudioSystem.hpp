#pragma once

#include "AssetLibrary.hpp"
#include "AudioTypes.hpp"
#include "Math.hpp"
#include "Scene.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace sat {

class PhysicsSystem;

enum class AudioBackend { Default, Null };

// Engine-thread only. The borrowed scene and the reader's captures must outlive use.
// The reader must not reenter the audio system or mutate the scene.
// stop() releases all voices and clips and preserves the most recent error.
class AudioSystem {
public:
    explicit AudioSystem(Scene& scene, AudioBackend backend = AudioBackend::Default);
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) = delete;
    AudioSystem& operator=(AudioSystem&&) = delete;

    // Stops an active session before replacing its reader.
    void setAssetReader(AssetReader reader);
    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] const std::string& error() const;
    void update(float deltaSeconds, const PhysicsSystem* physics = nullptr, float timeScale = 1.0f);

    [[nodiscard]] bool play(EntityId entity);
    [[nodiscard]] bool stopSound(EntityId entity);
    [[nodiscard]] bool pause(EntityId entity);
    [[nodiscard]] bool resume(EntityId entity);
    [[nodiscard]] bool isVirtualized(EntityId entity) const;
    [[nodiscard]] bool isPlaying(EntityId entity) const;
    [[nodiscard]] bool setVolume(EntityId entity, float volume);
    [[nodiscard]] bool setPitch(EntityId entity, float pitch);
    [[nodiscard]] bool playOneShot(std::string_view path, DVec3 position, float volume);
    [[nodiscard]] bool setBusVolume(AudioBus bus, float volume);
    void setMasterVolume(float volume);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
