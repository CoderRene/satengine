#include "module/Gravity.hpp"
#include "PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>

namespace sat {
DVec3 sampleGravity(std::span<const Entity *const> sources, DVec3 position) {
  DVec3 result{};
  for (const auto *entity : sources) {
    const auto &e = *entity;
    const auto &g = e.gravitySource;
    const DVec3 delta = e.transform.position - position;
    const double distance = std::hypot(delta.x, delta.y, delta.z);
    if (g.mode == GravityMode::Spherical) {
      const double radius = std::max(distance, g.minimumRadius);
      result = result + delta * (g.mu / radius / radius / radius);
    } else if (g.influenceRadius == 0 || distance <= g.influenceRadius) {
      const Vec3 acceleration = transformPoint(
          rotationMatrixEuler(e.transform.rotation), g.acceleration);
      result = result + DVec3{acceleration.x, acceleration.y, acceleration.z};
    }
  }
  return result;
}
DVec3 gravityAt(const Scene &scene, DVec3 position) {
  DVec3 result{};
  for (const auto &e : scene.entities()) {
    if (!e.hasGravitySource) {
      continue;
    }
    const Entity *source = &e;
    result = result + sampleGravity(std::span<const Entity *const>(&source, 1),
                                    position);
  }
  return result;
}
std::optional<Vec3> upAt(const Scene &scene, DVec3 position) {
  const DVec3 g = gravityAt(scene, position);
  const double magnitude = std::hypot(g.x, g.y, g.z);
  if (!std::isfinite(magnitude) || magnitude < 1e-8) {
    return std::nullopt;
  }
  return toVec3(g * (-1.0 / magnitude));
}
} // namespace sat
