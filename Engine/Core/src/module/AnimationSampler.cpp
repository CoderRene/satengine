#include "AnimationSampler.hpp"

#include <algorithm>
#include <cmath>

namespace sat {
namespace {
void normalizeQuaternion(float* value) {
    const double norm = std::sqrt(double(value[0]) * value[0] + double(value[1]) * value[1] + double(value[2]) * value[2] + double(value[3]) * value[3]);
    if (norm < 1e-12) { value[0] = value[1] = value[2] = 0; value[3] = 1; return; }
    for (int axis = 0; axis < 4; ++axis) { value[axis] = static_cast<float>(value[axis] / norm); }
}
void slerp(const float* source, const float* target, float alpha, float* result) {
    float a[4], b[4];
    std::copy_n(source, 4, a); std::copy_n(target, 4, b);
    normalizeQuaternion(a); normalizeQuaternion(b);
    float cosine = 0;
    for (int axis = 0; axis < 4; ++axis) { cosine += a[axis] * b[axis]; }
    if (cosine < 0) { for (float& value : b) { value = -value; } cosine = -cosine; }
    float left = 1 - alpha, right = alpha;
    if (cosine < 0.9995f) {
        const float angle = std::acos(std::clamp(cosine, 0.0f, 1.0f));
        const float sine = std::sin(angle);
        left = std::sin((1 - alpha) * angle) / sine;
        right = std::sin(alpha * angle) / sine;
    }
    for (int axis = 0; axis < 4; ++axis) { result[axis] = a[axis] * left + b[axis] * right; }
    normalizeQuaternion(result);
}
}
Mat4 animationMatrix(const AnimationPose& pose) {
    float q[4]; std::copy_n(pose.rotation, 4, q); normalizeQuaternion(q);
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
AnimationPose blendAnimationPose(const AnimationPose& source, const AnimationPose& target, float alpha) {
    AnimationPose result;
    result.translation = source.translation * (1-alpha) + target.translation * alpha;
    result.scale = source.scale * (1-alpha) + target.scale * alpha;
    slerp(source.rotation, target.rotation, alpha, result.rotation);
    return result;
}
float animationSampleTime(double time, float start, float end, bool loop) {
    const double duration = double(end) - start;
    if (duration <= 0 || !std::isfinite(time)) { return start; }
    if (loop) { time = std::fmod(time, duration); if (time < 0) { time += duration; } }
    return static_cast<float>(start + std::clamp(time, 0.0, duration));
}
void sampleAnimationChannel(const AnimationChannel& channel, float time, std::size_t& cursor, AnimationPose& pose) {
    if (channel.times.empty()) { return; }
    const auto last = channel.times.size() - 1;
    if (cursor >= last || time < channel.times[cursor] || time >= channel.times[cursor + 1]) {
        const auto found = std::upper_bound(channel.times.begin(), channel.times.end(), time);
        cursor = found == channel.times.begin() ? 0 : static_cast<std::size_t>(found - channel.times.begin() - 1);
    }
    const auto next = std::min(cursor + 1, last);
    const bool cubic = channel.interpolation == AnimationInterpolation::CubicSpline;
    const std::size_t width = channel.path == AnimationPath::Rotation ? 4 : 3;
    const std::size_t stride = width * (cubic ? 3 : 1);
    const float* a = channel.values.data() + cursor * stride + (cubic ? width : 0);
    const float* b = channel.values.data() + next * stride + (cubic ? width : 0);
    const float interval = channel.times[next] - channel.times[cursor];
    const float alpha = interval > 0 ? std::clamp((time - channel.times[cursor]) / interval, 0.0f, 1.0f) : 0;
    float result[4]{};
    if (channel.interpolation == AnimationInterpolation::Step || cursor == next) { std::copy_n(a, width, result); }
    else if (cubic) {
        const float a2 = alpha * alpha, a3 = a2 * alpha;
        for (std::size_t axis = 0; axis < width; ++axis) {
            result[axis] = (2*a3 - 3*a2 + 1)*a[axis] + (a3 - 2*a2 + alpha)*interval*a[width + axis] +
                (-2*a3 + 3*a2)*b[axis] + (a3 - a2)*interval*b[static_cast<int>(axis) - static_cast<int>(width)];
        }
    } else if (width == 4) { slerp(a, b, alpha, result); }
    else { for (std::size_t axis = 0; axis < width; ++axis) { result[axis] = a[axis] * (1-alpha) + b[axis] * alpha; } }
    if (channel.path == AnimationPath::Rotation) { normalizeQuaternion(result); std::copy_n(result, 4, pose.rotation); }
    else if (channel.path == AnimationPath::Translation) { pose.translation = {result[0], result[1], result[2]}; }
    else { pose.scale = {result[0], result[1], result[2]}; }
}
} // namespace sat
