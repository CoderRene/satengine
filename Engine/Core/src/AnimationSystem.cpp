#include "AnimationSystem.hpp"

#include "AnimationAsset.hpp"
#include "AnimationGraph.hpp"
#include "AssetImportSettings.hpp"
#include "module/AnimationSampler.hpp"
#include "module/AnimationStateMachine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace sat {
struct AnimationSystem::Impl {
    struct Rig { AnimationData data; float scale = 1; std::vector<std::size_t> order; };
    struct BoundClip {
        const AnimationClip* clip = nullptr;
        std::vector<int> joints;
        float start = 0, end = 0, speed = 1;
    };
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
    };
    struct Instance {
        std::string meshPath;
        AnimatorComponent settings;
        Rig* rig = nullptr;
        const AnimationGraph* graph = nullptr;
        std::unordered_map<std::string, BoundClip> bindings;
        std::unordered_map<std::string, std::size_t, NameHash, std::equal_to<>> parameterIndices;
        std::vector<double> parameters;
        std::unordered_set<std::string> reportedParameters;
        std::vector<AnimationPose> pose, source;
        std::vector<Mat4> worlds;
        std::vector<bool> animated;
        std::vector<std::size_t> cursors;
        BoundClip* clip = nullptr;
        BoundClip* sourceClip = nullptr;
        double sourceTime = 0;
        float sourceSpeed = 1;
        bool sourceLoop = true, frozenSource = false;
        std::vector<std::size_t> sourceCursors;
        std::uint32_t state = 0;
        std::size_t offset = 0;
        double time = 0;
        float speed = 1, fade = 0, fadeTime = 0;
        bool playing = false, loop = true;
    };
    explicit Impl(Scene& value, Mode modeIn) : scene(value), mode(modeIn) {}
    Scene& scene;
    const Mode mode;
    AssetReader reader;
    std::unordered_map<std::string, Rig> rigs;
    std::unordered_map<std::string, AnimationClipAsset> clips;
    std::unordered_map<std::string, AnimationGraph> graphs;
    std::unordered_map<EntityId, Instance> instances;
    std::unordered_map<EntityId, std::uint64_t> failedInstances;
    std::vector<Mat4> palettes;
    std::string error;
    std::uint64_t revision = 0;
    bool running = false;

    bool read(std::string_view path, std::string& bytes) {
        if (!reader || reader(path, bytes, error) != AssetReadResult::Found) {
            error = "Cannot read animation asset " + std::string(path) + ": " + error; return false;
        }
        return true;
    }
    Rig* rig(std::string_view path) {
        const std::string key(path);
        if (auto found = rigs.find(key); found != rigs.end()) { return &found->second; }
        std::string bytes;
        Rig candidate;
        if (!read(path, bytes) || !decodeGltfAnimation(bytes, path, reader, candidate.data, error)) { return nullptr; }
        std::string metadata;
        const auto metadataResult = reader(key + ".satimport", metadata, error);
        AssetImportSettings settings;
        if (metadataResult == AssetReadResult::Error ||
            (metadataResult == AssetReadResult::Found && !parseImportSettings(metadata, settings, error))) { return nullptr; }
        candidate.scale = settings.scale;
        // Topological order is cached once. cgltf validates the hierarchy.
        std::vector<bool> visited(candidate.data.joints.size());
        const auto visit = [&](auto&& self, std::size_t index) -> void {
            if (visited[index]) { return; }
            visited[index] = true;
            const int parent = candidate.data.joints[index].parent;
            if (parent >= 0) { self(self, static_cast<std::size_t>(parent)); }
            candidate.order.push_back(index);
        };
        for (std::size_t i = 0; i < visited.size(); ++i) { visit(visit, i); }
        return &rigs.emplace(key, std::move(candidate)).first->second;
    }
    BoundClip* bind(Instance& instance, std::string_view reference) {
        const std::string key(reference);
        if (auto found = instance.bindings.find(key); found != instance.bindings.end()) { return &found->second; }
        BoundClip bound;
        if (isAnimationClipAssetPath(reference)) {
            auto found = clips.find(key);
            if (found == clips.end()) {
                std::string bytes;
                AnimationClipAsset asset;
                if (!read(reference, bytes) || !decodeAnimationClip(bytes, asset, error)) { return nullptr; }
                found = clips.emplace(key, std::move(asset)).first;
            }
            bound.clip = &found->second.clip;
            bound.joints = bindAnimationClip(found->second, instance.rig->data);
            bound.start = found->second.startSeconds; bound.end = found->second.endSeconds; bound.speed = found->second.speed;
            if (std::find(bound.joints.begin(), bound.joints.end(), -1) != bound.joints.end()) {
                std::cerr << "Animation clip " << key << " has joints absent from " << instance.meshPath << "; those channels are skipped\n";
            }
        } else {
            for (const auto& clip : instance.rig->data.clips) {
                if (reference.empty() || clip.name == reference) { bound.clip = &clip; break; }
            }
            if (!bound.clip) { error = "Unknown embedded animation clip " + key; return nullptr; }
            bound.end = bound.clip->duration;
            bound.joints.resize(instance.rig->data.joints.size());
            std::iota(bound.joints.begin(), bound.joints.end(), 0);
        }
        return &instance.bindings.emplace(key, std::move(bound)).first->second;
    }
    bool select(Instance& instance, std::string_view reference, bool loop, float fade) {
        auto* clip = bind(instance, reference);
        if (!clip) { return false; }
        instance.frozenSource = instance.fadeTime < instance.fade;
        instance.source = instance.pose;
        instance.sourceClip = instance.clip;
        instance.sourceTime = instance.time;
        instance.sourceLoop = instance.loop;
        instance.sourceSpeed = instance.graph ? instance.graph->states[instance.state].speed : 1;
        instance.sourceCursors = instance.cursors;
        instance.clip = clip;
        instance.time = 0;
        instance.loop = loop;
        instance.playing = true;
        instance.fade = fade;
        instance.fadeTime = 0;
        std::fill(instance.cursors.begin(), instance.cursors.end(), 0);
        return true;
    }
    Instance* ensure(EntityId id) {
        const auto failed = failedInstances.find(id);
        if (failed != failedInstances.end() && failed->second == scene.editRevision()) { return nullptr; }
        auto* result = createInstance(id);
        if (!result) { failedInstances[id] = scene.editRevision(); }
        else { failedInstances.erase(id); }
        return result;
    }
    Instance* createInstance(EntityId id) {
        const auto* entity = scene.entityById(id);
        if (!entity || !entity->hasAnimator || !entity->hasMesh || entity->mesh.meshNode || !isGltfAssetPath(entity->mesh.meshPath)) {
            error = "Animator requires a complete glTF mesh"; return nullptr;
        }
        auto found = instances.find(id);
        if (found != instances.end() && found->second.meshPath == entity->mesh.meshPath && found->second.settings == entity->animator) { return &found->second; }
        if (found != instances.end()) { instances.erase(found); }
        Instance instance;
        instance.meshPath = entity->mesh.meshPath;
        instance.settings = entity->animator;
        instance.speed = entity->animator.speed;
        instance.rig = rig(instance.meshPath);
        if (!instance.rig || instance.rig->data.joints.empty()) { return nullptr; }
        const auto count = instance.rig->data.joints.size();
        instance.pose.reserve(count);
        for (const auto& joint : instance.rig->data.joints) { instance.pose.push_back(joint.restTRS); }
        instance.source = instance.pose;
        instance.worlds.resize(count);
        instance.animated.resize(count);
        instance.cursors.resize(kMaxAnimationJoints * 3);
        instance.sourceCursors.resize(kMaxAnimationJoints * 3);
        if (!entity->animator.controllerPath.empty()) {
            auto graph = graphs.find(entity->animator.controllerPath);
            if (graph == graphs.end()) {
                std::string bytes;
                AnimationGraph candidate;
                if (!read(entity->animator.controllerPath, bytes) || !decodeAnimationGraph(bytes, candidate, error)) { return nullptr; }
                graph = graphs.emplace(entity->animator.controllerPath, std::move(candidate)).first;
            }
            instance.graph = &graph->second;
            instance.state = instance.graph->defaultState;
            for (const auto& parameter : instance.graph->parameters) {
                instance.parameterIndices.emplace(parameter.name, instance.parameters.size());
                instance.parameters.push_back(parameter.value);
            }
            // Load and bind all state clips before updates enter the hot path.
            for (const auto& state : instance.graph->states) { if (!bind(instance, state.clip)) { return nullptr; } }
            const auto& state = instance.graph->states[instance.state];
            if (!select(instance, state.clip, state.loop, 0)) { return nullptr; }
        } else if (!instance.rig->data.clips.empty() || !entity->animator.clip.empty()) {
            if (!select(instance, entity->animator.clip, entity->animator.loop, 0)) { return nullptr; }
        }
        instance.playing = mode == Mode::Runtime && instance.clip && (instance.graph || entity->animator.playOnStart);
        return &instances.emplace(id, std::move(instance)).first->second;
    }
    void evaluate(Instance& instance) {
        const auto& joints = instance.rig->data.joints;
        for (std::size_t index = 0; index < joints.size(); ++index) { instance.pose[index] = joints[index].restTRS; }
        std::fill(instance.animated.begin(), instance.animated.end(), false);
        if (instance.clip) {
            const auto& bound = *instance.clip;
            const float sample = animationSampleTime(instance.time, bound.start, bound.end, instance.loop);
            for (std::size_t index = 0; index < bound.clip->channels.size(); ++index) {
                const auto& channel = bound.clip->channels[index];
                const int joint = bound.joints[channel.joint];
                if (joint < 0) { continue; }
                sampleAnimationChannel(channel, sample, instance.cursors[index], instance.pose[joint]);
                instance.animated[joint] = true;
            }
        }
        if (instance.fadeTime < instance.fade) {
            if (instance.sourceClip && !instance.frozenSource) {
                for (std::size_t index = 0; index < joints.size(); ++index) { instance.source[index] = joints[index].restTRS; }
                const auto& source = *instance.sourceClip;
                const auto sourceTime = animationSampleTime(instance.sourceTime, source.start, source.end, instance.sourceLoop);
                for (std::size_t index = 0; index < source.clip->channels.size(); ++index) {
                    const auto& channel = source.clip->channels[index]; const int joint = source.joints[channel.joint];
                    if (joint >= 0) { sampleAnimationChannel(channel, sourceTime, instance.sourceCursors[index], instance.source[joint]); }
                }
            }
            const float alpha = instance.fadeTime / instance.fade;
            for (std::size_t index = 0; index < joints.size(); ++index) {
                instance.pose[index] = blendAnimationPose(instance.source[index], instance.pose[index], alpha);
                // Matrix nodes cannot be animation targets. Preserve their matrices below.
                if (instance.source[index].translation != joints[index].restTRS.translation ||
                    instance.source[index].scale != joints[index].restTRS.scale ||
                    !std::equal(std::begin(instance.source[index].rotation), std::end(instance.source[index].rotation), joints[index].restTRS.rotation)) {
                    instance.animated[index] = true;
                }
            }
        }
        for (const auto index : instance.rig->order) {
            const auto& joint = joints[index];
            const auto local = instance.animated[index] ? animationMatrix(instance.pose[index]) : joint.restLocal;
            instance.worlds[index] = joint.parent >= 0 ? instance.worlds[joint.parent] * local : local;
            auto& palette = palettes[instance.offset + index];
            palette = instance.worlds[index] * joint.inverseBind;
            palette.m[12] *= instance.rig->scale; palette.m[13] *= instance.rig->scale; palette.m[14] *= instance.rig->scale;
        }
    }
    bool parameter(EntityId id, std::string_view name, AnimationParameterType type, double value) {
        auto* instance = ensure(id);
        if (!instance || !instance->graph || !std::isfinite(value)) { return false; }
        const auto found = instance->parameterIndices.find(name);
        if (found == instance->parameterIndices.end() || instance->graph->parameters[found->second].type != type) {
            if (instance->reportedParameters.insert(std::string(name)).second) {
                std::cerr << "Entity " << id << ": unknown animation parameter or incorrect type: " << name << '\n';
            }
            return false;
        }
        instance->parameters[found->second] = value;
        return true;
    }
};
AnimationSystem::AnimationSystem(Scene& scene, Mode mode) : m_impl(std::make_unique<Impl>(scene, mode)) {}
AnimationSystem::~AnimationSystem() = default;
void AnimationSystem::setAssetReader(AssetReader reader) {
    stop();
    m_impl->error.clear();
    m_impl->reader = std::move(reader);
}
void AnimationSystem::setAssetRevision(std::uint64_t revision) {
    if (m_impl->revision == revision) { return; }
    const bool active = m_impl->running;
    stop(); m_impl->revision = revision;
    if (active) { (void)start(); }
}
bool AnimationSystem::start() { m_impl->running = true; m_impl->error.clear(); update(0); return m_impl->error.empty(); }
void AnimationSystem::stop() {
    m_impl->running = false; m_impl->instances.clear(); m_impl->failedInstances.clear(); m_impl->palettes.clear();
    m_impl->rigs.clear(); m_impl->clips.clear(); m_impl->graphs.clear();
}
void AnimationSystem::update(float deltaSeconds, float timeScale) {
    auto& impl = *m_impl;
    if (!impl.running || !std::isfinite(deltaSeconds) || deltaSeconds < 0 || !std::isfinite(timeScale) || timeScale < 0) { return; }
    std::erase_if(impl.instances, [&](const auto& entry) {
        const auto* entity = impl.scene.entityById(entry.first);
        return !entity || !entity->hasAnimator || !entity->hasMesh || entity->mesh.meshNode || !isGltfAssetPath(entity->mesh.meshPath);
    });
    std::size_t count = 0;
    for (const auto& entity : impl.scene.entities()) {
        if (!entity.hasAnimator) { continue; }
        auto* instance = impl.ensure(entity.id());
        if (!instance) { continue; }
        instance->offset = count; count += instance->pose.size();
    }
    impl.palettes.resize(count);
    for (auto& [id, instance] : impl.instances) {
        const double dt = double(deltaSeconds) * timeScale;
        if (instance.playing && instance.clip) {
            const float stateSpeed = instance.graph ? instance.graph->states[instance.state].speed : 1;
            instance.time += dt * instance.speed * instance.clip->speed * stateSpeed;
            if (instance.sourceClip && !instance.frozenSource) { instance.sourceTime += dt * instance.speed * instance.sourceClip->speed * instance.sourceSpeed; }
            instance.fadeTime = static_cast<float>(std::min(double(instance.fade), instance.fadeTime + dt));
            if (instance.graph) {
                const double duration = instance.clip->end - instance.clip->start;
                const int transition = animationTransition(*instance.graph, instance.state, duration > 0 ? instance.time / duration : 1,
                    instance.parameters, instance.fadeTime < instance.fade);
                if (transition >= 0) {
                    const auto& edge = instance.graph->transitions[transition];
                    const auto& target = instance.graph->states[edge.target];
                    if (impl.select(instance, target.clip, target.loop, edge.durationSeconds)) {
                        instance.state = edge.target;
                        consumeAnimationTriggers(*instance.graph, edge, instance.parameters);
                    }
                }
            }
            if (!instance.loop && instance.time >= instance.clip->end - instance.clip->start) {
                instance.time = instance.clip->end - instance.clip->start;
                if (!instance.graph) { instance.playing = false; }
            }
        }
        impl.evaluate(instance);
    }
}
bool AnimationSystem::play(EntityId id, std::string_view reference, bool loop, float fade) {
    m_impl->running = true;
    if (!std::isfinite(fade) || fade < 0) { return false; }
    auto* instance = m_impl->ensure(id);
    if (!instance || !m_impl->select(*instance, reference, loop, fade)) { return false; }
    instance->graph = nullptr;
    return true;
}
bool AnimationSystem::resume(EntityId id) {
    auto* instance = m_impl->ensure(id);
    if (!instance || !instance->clip) { return false; }
    m_impl->running = true;
    instance->playing = true;
    return true;
}
void AnimationSystem::stop(EntityId id) { if (auto found = m_impl->instances.find(id); found != m_impl->instances.end()) { found->second.playing = false; } }
bool AnimationSystem::setSpeed(EntityId id, float speed) {
    auto* instance = m_impl->ensure(id); if (!instance || !std::isfinite(speed)) { return false; } instance->speed = speed; return true;
}
bool AnimationSystem::setTime(EntityId id, float time) {
    auto* instance = m_impl->ensure(id);
    if (!instance || !std::isfinite(time)) { return false; }
    instance->time = time;
    if (m_impl->mode == Mode::Preview) { m_impl->running = true; }
    return true;
}
float AnimationSystem::time(EntityId id) const { const auto found = m_impl->instances.find(id); return found == m_impl->instances.end() ? 0 : static_cast<float>(found->second.time); }
bool AnimationSystem::isPlaying(EntityId id) const { const auto found = m_impl->instances.find(id); return found != m_impl->instances.end() && found->second.playing; }
std::vector<std::string> AnimationSystem::clipNames(std::string_view path) {
    std::vector<std::string> names;
    if (const auto* rig = m_impl->rig(path)) { for (const auto& clip : rig->data.clips) { names.push_back(clip.name); } }
    return names;
}
std::span<const Mat4> AnimationSystem::palette(EntityId id) const {
    const auto found = m_impl->instances.find(id);
    if (found == m_impl->instances.end() || found->second.offset + found->second.pose.size() > m_impl->palettes.size()) { return {}; }
    return {m_impl->palettes.data() + found->second.offset, found->second.pose.size()};
}
bool AnimationSystem::setBool(EntityId id, std::string_view name, bool value) { return m_impl->parameter(id, name, AnimationParameterType::Bool, value); }
bool AnimationSystem::setFloat(EntityId id, std::string_view name, float value) { return m_impl->parameter(id, name, AnimationParameterType::Float, value); }
bool AnimationSystem::setInt(EntityId id, std::string_view name, std::int32_t value) { return m_impl->parameter(id, name, AnimationParameterType::Int, value); }
bool AnimationSystem::setTrigger(EntityId id, std::string_view name) { return m_impl->parameter(id, name, AnimationParameterType::Trigger, 1); }
std::string_view AnimationSystem::currentState(EntityId id) const {
    const auto found = m_impl->instances.find(id);
    return found != m_impl->instances.end() && found->second.graph ? found->second.graph->states[found->second.state].name : std::string_view{};
}
bool AnimationSystem::crossfadeTo(EntityId id, std::string_view name, float seconds) {
    auto* instance = m_impl->ensure(id);
    if (!instance || !instance->graph || !std::isfinite(seconds) || seconds < 0) { return false; }
    for (std::size_t index = 0; index < instance->graph->states.size(); ++index) {
        const auto& state = instance->graph->states[index];
        if (state.name == name && m_impl->select(*instance, state.clip, state.loop, seconds)) {
            instance->state = static_cast<std::uint32_t>(index); return true;
        }
    }
    return false;
}
AnimationDebugView AnimationSystem::debugView(EntityId id) const {
    const auto found = m_impl->instances.find(id);
    if (found == m_impl->instances.end()) { return {}; }
    const auto& instance = found->second;
    return {currentState(id), instance.fade > 0 ? std::min(instance.fadeTime / instance.fade, 1.0f) : 1, instance.parameters};
}
const std::string& AnimationSystem::error() const { return m_impl->error; }
} // namespace sat
