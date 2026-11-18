#pragma once
#include "AssetData.hpp"
#include "Scene.hpp"
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
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
