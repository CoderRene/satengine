#pragma once
#include "AssetData.hpp"
#include "Scene.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
// Resolves a mesh directly or a single glTF or FBX asset represented by descendant mesh nodes.
[[nodiscard]] std::string_view animatorMeshPath(const Scene& scene, const Entity& entity);

// A dense lookup token valid for the layout that issued it.
struct AnimationInstanceHandle {
    std::uint64_t layoutRevision = 0;
    std::uint32_t index = ~std::uint32_t{};

    bool operator==(const AnimationInstanceHandle&) const = default;
};

struct AnimationMeshPose {
    std::span<const Mat4> palette;
    AnimationBounds bounds;
    std::uint32_t paletteOffset = 0;
    AnimationInstanceHandle handle;
};

// Radius of the conservative world-space sphere used by the renderer.
[[nodiscard]] float animationOcclusionWorldRadius(const Entity& entity);

struct AnimationOcclusionProbe {
    EntityId entity;
    Transform transform;
    float radius;
};

struct AnimationOcclusionStamp {
    std::uint64_t frame = 0, generation = 0, layout = 0, edit = 0, destruction = 0;
    EntityId camera = kInvalidEntityId;
    Transform cameraTransform;
    float fieldOfView = 0, nearClip = 0, farClip = 0;
};

struct AnimationDebugView {
    std::string_view activeState;
    float transitionProgress = 1;
    std::span<const double> parameters;
};
// Window-thread only. Scene and reader captures must outlive this system.
// Palette/debug spans remain valid until update, playback edits, invalidation or stop.
class AnimationSystem {
public:
    enum class Mode { Runtime, Preview };
    explicit AnimationSystem(Scene& scene, Mode mode = Mode::Runtime);
    ~AnimationSystem();
    AnimationSystem(const AnimationSystem&) = delete;
    AnimationSystem& operator=(const AnimationSystem&) = delete;
    AnimationSystem(AnimationSystem&&) = delete;
    AnimationSystem& operator=(AnimationSystem&&) = delete;
    void setAssetReader(AssetReader reader);
    void setAssetRevision(std::uint64_t revision);
    [[nodiscard]] bool start();
    void stop();
    void update(float deltaSeconds, float timeScale = 1);
    [[nodiscard]] bool play(EntityId entity, std::string_view clip, bool loop = true, float fade = 0.2f);
    [[nodiscard]] bool resume(EntityId entity);
    void stop(EntityId entity);
    [[nodiscard]] bool setSpeed(EntityId entity, float speed);
    [[nodiscard]] bool setTime(EntityId entity, float time);
    [[nodiscard]] float time(EntityId entity) const;
    [[nodiscard]] bool isPlaying(EntityId entity) const;
    [[nodiscard]] std::vector<std::string> clipNames(std::string_view meshPath);
    [[nodiscard]] std::span<const Mat4> palette(EntityId entity) const;
    [[nodiscard]] std::span<const Mat4> meshPalette(const Entity& meshEntity) const;
    [[nodiscard]] AnimationMeshPose meshPose(const Entity& meshEntity) const;
    // Enable only when the runtime has a single view supplying opaque-depth feedback.
    void enableOcclusionFeedback();
    // Runtime renderer feedback. Readbacks are delayed and must carry their original snapshot.
    [[nodiscard]] AnimationOcclusionStamp collectOcclusionProbes(std::vector<AnimationOcclusionProbe>& probes) const;
    void submitOcclusion(const AnimationOcclusionStamp& stamp, std::span<const AnimationOcclusionProbe> probes,
        std::span<const std::uint32_t> occluded);
    void invalidateOcclusion();
    // Restore a skipped pose before rendering when a possible blocker has moved.
    [[nodiscard]] bool refreshOcclusionPoses(bool geometryMoved = true);
    [[nodiscard]] std::span<const Mat4> palettes() const;
    [[nodiscard]] std::uint64_t layoutRevision() const;
    [[nodiscard]] AnimationBounds bounds(AnimationInstanceHandle handle) const;
    [[nodiscard]] std::uint64_t poseRevision(AnimationInstanceHandle handle) const;
    [[nodiscard]] std::uint64_t poseRevision() const;
    [[nodiscard]] bool setBool(EntityId entity, std::string_view name, bool value);
    [[nodiscard]] bool setFloat(EntityId entity, std::string_view name, float value);
    [[nodiscard]] bool setInt(EntityId entity, std::string_view name, std::int32_t value);
    [[nodiscard]] bool setTrigger(EntityId entity, std::string_view name);
    [[nodiscard]] std::string_view currentState(EntityId entity) const;
    [[nodiscard]] bool crossfadeTo(EntityId entity, std::string_view state, float seconds);
    [[nodiscard]] AnimationDebugView debugView(EntityId entity) const;
    [[nodiscard]] const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace sat
