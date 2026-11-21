#pragma once

#include <algorithm>
#include <cmath>

namespace sat {

constexpr float kDefaultSceneCameraSpeed = 8.0f;
constexpr float kMinSceneCameraSpeed = 0.1f;
constexpr float kMaxSceneCameraSpeed = 1000.0f;

[[nodiscard]] inline float adjustSceneCameraSpeed(float speed, float wheelDelta) {
    constexpr float kSpeedFactorPerStep = 1.25f;
    return std::clamp(speed * std::pow(kSpeedFactorPerStep, wheelDelta), kMinSceneCameraSpeed, kMaxSceneCameraSpeed);
}

} // namespace sat
