#include "AnimationSampler.hpp"

#include <algorithm>
#include <cmath>

namespace sat {
namespace {
float quaternionDot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}
void interpolateQuaternion(const float* source, const float* target, float left, float right, float* result) {
    for (int axis = 0; axis < 4; ++axis) { result[axis] = source[axis] * left + target[axis] * right; }
    normalizeQuaternion(result);
}
void slerp(const float* source, const float* target, float alpha, float* result) {
    const float cosine = quaternionDot(source, target);
    const float sign = cosine < 0 ? -1.0f : 1.0f;
    const float absolute = std::abs(cosine);
    if (absolute >= 0.9995f) {
        interpolateQuaternion(source, target, 1 - alpha, alpha * sign, result);
        return;
    }
    const float angle = std::acos(absolute);
    const float invSine = 1.0f / std::sin(angle);
    interpolateQuaternion(source, target, std::sin((1 - alpha) * angle) * invSine,
        std::sin(alpha * angle) * invSine * sign, result);
}
void blendRotation(const float* source, const float* target, float alpha, float* result, AnimationRotationBlend mode) {
    if (mode == AnimationRotationBlend::Nlerp) { nlerp(source, target, alpha, result); }
    else { slerp(source, target, alpha, result); }
}

template <int Width>
void sampleTrack(const float* times, std::size_t count, AnimationInterpolation interpolation, float time,
    std::size_t& cursor, float* result, AnimationRotationBlend mode) {
    const auto last = count - 1;
    if (time <= times[0]) { cursor = 0; }
    else if (time >= times[last]) { cursor = last; }
    else if (cursor >= last || time < times[cursor] || time >= times[cursor + 1]) {
        if (cursor < last && time >= times[cursor + 1] &&
            (cursor + 1 == last || time < times[cursor + 2])) {
            ++cursor;
        } else {
            cursor = static_cast<std::size_t>(std::upper_bound(times, times + count, time) - times - 1);
        }
    }
    const bool cubic = interpolation == AnimationInterpolation::CubicSpline;
    const std::size_t stride = Width * (cubic ? 3 : 1);
    const float* a = times + count + cursor * stride + (cubic ? Width : 0);
    if (interpolation == AnimationInterpolation::Step || cursor == last || time <= times[0]) {
        std::copy_n(a, Width, result);
        if constexpr (Width == 4) {
            if (cubic) { normalizeQuaternion(result); }
        }
        return;
    }
    const float* b = a + stride;
    const float interval = times[cursor + 1] - times[cursor];
    const float alpha = (time - times[cursor]) / interval;
    if (cubic) {
        const float a2 = alpha * alpha, a3 = a2 * alpha;
        const float h00 = 2*a3 - 3*a2 + 1, h10 = (a3 - 2*a2 + alpha) * interval;
        const float h01 = -2*a3 + 3*a2, h11 = (a3 - a2) * interval;
        for (int axis = 0; axis < Width; ++axis) {
            result[axis] = h00*a[axis] + h10*a[Width + axis] + h01*b[axis] + h11*b[axis - Width];
        }
        if constexpr (Width == 4) { normalizeQuaternion(result); }
    } else if constexpr (Width == 4) {
        blendRotation(a, b, alpha, result, mode);
    } else {
        for (int axis = 0; axis < Width; ++axis) { result[axis] = a[axis] * (1-alpha) + b[axis] * alpha; }
    }
}
}
void normalizeQuaternion(float* value) {
    float squared = quaternionDot(value, value);
    if (!std::isfinite(squared)) {
        const float scale = std::max({std::abs(value[0]), std::abs(value[1]), std::abs(value[2]), std::abs(value[3])});
        if (std::isfinite(scale)) {
            for (int axis = 0; axis < 4; ++axis) { value[axis] /= scale; }
            squared = quaternionDot(value, value);
        }
    }
    if (!std::isfinite(squared) || squared < 1e-24f) {
        value[0] = value[1] = value[2] = 0; value[3] = 1;
        return;
    }
    const float inverseLength = 1.0f / std::sqrt(squared);
    for (int axis = 0; axis < 4; ++axis) { value[axis] *= inverseLength; }
}
void nlerp(const float* source, const float* target, float alpha, float* result) {
    const float sign = quaternionDot(source, target) < 0 ? -1.0f : 1.0f;
    interpolateQuaternion(source, target, 1 - alpha, alpha * sign, result);
}
CompiledClip compileAnimationClip(const AnimationClip& clip, std::span<const int> remap) {
    CompiledClip result;
    result.tracks.reserve(clip.channels.size());
    std::size_t keySize = 0;
    for (const auto& channel : clip.channels) {
        if (channel.times.empty()) { continue; }
        const int joint = remap.empty() ? channel.joint : remap[channel.joint];
        if (joint < 0) { continue; }
        result.tracks.push_back({static_cast<std::uint16_t>(joint), channel.path, channel.interpolation,
            static_cast<std::uint32_t>(keySize), static_cast<std::uint32_t>(channel.times.size())});
        keySize += channel.times.size() + channel.values.size();
    }
    result.keys.reserve(keySize);
    std::size_t trackIndex = 0;
    for (const auto& channel : clip.channels) {
        if (channel.times.empty() || (!remap.empty() && remap[channel.joint] < 0)) { continue; }
        result.keys.insert(result.keys.end(), channel.times.begin(), channel.times.end());
        const auto values = result.keys.size();
        result.keys.insert(result.keys.end(), channel.values.begin(), channel.values.end());
        const auto& track = result.tracks[trackIndex++];
        // Cubic spline values and tangents define one curve. Normalize its sampled quaternion.
        if (track.path == AnimationPath::Rotation && track.interpolation != AnimationInterpolation::CubicSpline) {
            for (std::size_t i = values; i < result.keys.size(); i += 4) { normalizeQuaternion(result.keys.data() + i); }
        }
    }
    std::stable_sort(result.tracks.begin(), result.tracks.end(),
        [](const auto& a, const auto& b) { return a.joint < b.joint; });
    return result;
}
Mat4 animationMatrix(const AnimationPose& pose) {
    const auto& q = pose.rotation;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 result;
    result.m[0] = (1 - 2 * (y*y + z*z)) * pose.scale.x;
    result.m[1] = 2 * (x*y + z*w) * pose.scale.x;
    result.m[2] = 2 * (x*z - y*w) * pose.scale.x;
    result.m[4] = 2 * (x*y - z*w) * pose.scale.y;
    result.m[5] = (1 - 2 * (x*x + z*z)) * pose.scale.y;
    result.m[6] = 2 * (y*z + x*w) * pose.scale.y;
    result.m[8] = 2 * (x*z + y*w) * pose.scale.z;
    result.m[9] = 2 * (y*z - x*w) * pose.scale.z;
    result.m[10] = (1 - 2 * (x*x + y*y)) * pose.scale.z;
    result.m[12] = pose.translation.x; result.m[13] = pose.translation.y; result.m[14] = pose.translation.z;
    return result;
}
AnimationPose blendAnimationPose(const AnimationPose& source, const AnimationPose& target, float alpha,
    AnimationRotationBlend mode) {
    AnimationPose result;
    result.translation = source.translation * (1-alpha) + target.translation * alpha;
    result.scale = source.scale * (1-alpha) + target.scale * alpha;
    blendRotation(source.rotation, target.rotation, alpha, result.rotation, mode);
    return result;
}
float animationSampleTime(double time, float start, float end, bool loop) {
    const double duration = double(end) - start;
    if (duration <= 0 || !std::isfinite(time)) { return start; }
    if (loop) { time = std::fmod(time, duration); if (time < 0) { time += duration; } }
    return static_cast<float>(start + std::clamp(time, 0.0, duration));
}
void sampleAnimationTrack(const CompiledClip& clip, const CompiledAnimationTrack& track, float time, std::size_t& cursor,
    AnimationPose& pose, AnimationRotationBlend mode) {
    const float* times = clip.keys.data() + track.keyOffset;
    if (track.path == AnimationPath::Rotation) {
        sampleTrack<4>(times, track.keyCount, track.interpolation, time, cursor, pose.rotation, mode);
    } else {
        float result[3];
        sampleTrack<3>(times, track.keyCount, track.interpolation, time, cursor, result, mode);
        auto& value = track.path == AnimationPath::Translation ? pose.translation : pose.scale;
        value = {result[0], result[1], result[2]};
    }
}
} // namespace sat
