#pragma once
#include "AssetData.hpp"
#include "Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sat {
struct CompiledAnimationTrack {
    std::uint16_t joint;
    AnimationPath path;
    AnimationInterpolation interpolation;
    std::uint32_t keyOffset, keyCount;
};

struct CompiledClip {
    std::vector<float> keys;
    std::vector<CompiledAnimationTrack> tracks;
};

// An empty remap binds an embedded clip directly to its rig.
[[nodiscard]] CompiledClip compileAnimationClip(const AnimationClip& clip, std::span<const int> remap);
void normalizeQuaternion(float* value);
void nlerp(const float* source, const float* target, float alpha, float* result);
[[nodiscard]] Mat4 animationMatrix(const AnimationPose& pose);
[[nodiscard]] AnimationPose blendAnimationPose(const AnimationPose& source, const AnimationPose& target, float alpha,
    AnimationRotationBlend mode);
[[nodiscard]] float animationSampleTime(double time, float start, float end, bool loop);
void sampleAnimationTrack(const CompiledClip& clip, const CompiledAnimationTrack& track, float time, std::size_t& cursor,
    AnimationPose& pose, AnimationRotationBlend mode);
} // namespace sat
