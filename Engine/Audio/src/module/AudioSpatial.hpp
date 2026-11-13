#pragma once

#include "Math.hpp"
#include "Scene.hpp"

#include <cmath>

namespace sat {

inline const Entity* audioListener(const Scene& scene) {
    const Entity* camera = nullptr;
    for (const auto& entity : scene.entities()) {
        if (entity.hasAudioListener) {
            return &entity;
        }
        if (entity.hasCamera && entity.camera.isMain) {
            camera = &entity;
        }
    }
    return camera;
}

struct AudioBasis {
    Vec3 right;
    Vec3 up;
    Vec3 forward;

    // Project double-precision offsets into miniaudio's right-handed space.
    [[nodiscard]] Vec3 toMixer(DVec3 offset) const {
        const auto project = [&](Vec3 axis) {
            return static_cast<float>(offset.x * axis.x + offset.y * axis.y + offset.z * axis.z);
        };
        return {project(right), project(up), -project(forward)};
    }
};

inline AudioBasis audioBasis(Vec3 rotation) {
    const Vec3 forward = cameraForward(rotation.x, rotation.y);
    const float yaw = rotation.y * kDegToRad;
    const float roll = rotation.z * kDegToRad;
    const Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const Vec3 up = cross(forward, right);
    const float sinRoll = std::sin(roll);
    const float cosRoll = std::cos(roll);
    return {right * cosRoll + up * sinRoll, up * cosRoll - right * sinRoll, forward};
}

} // namespace sat
