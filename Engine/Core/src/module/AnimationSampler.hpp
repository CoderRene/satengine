#pragma once
#include "AssetData.hpp"
#include <span>

namespace sat {
[[nodiscard]] Mat4 animationMatrix(const AnimationPose& pose);
[[nodiscard]] AnimationPose blendAnimationPose(const AnimationPose& source, const AnimationPose& target, float alpha);
[[nodiscard]] float animationSampleTime(double time, float start, float end, bool loop);
void sampleAnimationChannel(const AnimationChannel& channel, float time, std::size_t& cursor, AnimationPose& pose);
} // namespace sat
