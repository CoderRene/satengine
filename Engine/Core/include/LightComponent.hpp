#pragma once

namespace sat {

enum class LightType { Directional, Point, Spot };

// Direction follows the entity rotation's +Z axis. Range is in world units.
// Spot cone angles are half-angles in degrees. Colors and intensity are linear.
struct LightComponent {
    LightType type = LightType::Directional;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 35.0f;
    bool castShadow = false;
    bool operator==(const LightComponent&) const = default;

};

} // namespace sat
