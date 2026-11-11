#include "ColliderLines.hpp"

#include <cmath>

namespace sat {
void appendColliderLines(const Entity &e, DVec3 origin, bool selected,
                         std::vector<DebugLine> &lines) {
  if (!e.hasCollider) {
    return;
  }
  const Vec3 color = selected             ? Vec3{1, 0.7f, 0.1f}
                     : e.collider.trigger ? Vec3{0.3f, 0.7f, 1}
                                          : Vec3{0.2f, 1, 0.3f};
  const Mat4 parent = matrixFromTransform(e.transform, origin);
  for (const auto &s : e.collider.shapes) {
    const Mat4 world =
        parent * translationMatrix(s.offset) * rotationMatrixEuler(s.rotation);
    const auto line = [&](Vec3 a, Vec3 b) {
      lines.push_back(
          {transformPoint(world, a), transformPoint(world, b), color});
    };
    if (s.shape == ColliderShape::Box) {
      Vec3 corners[8];
      for (int i = 0; i < 8; ++i) {
        corners[i] = {(i & 1 ? 1 : -1) * s.halfExtents.x,
                      (i & 2 ? 1 : -1) * s.halfExtents.y,
                      (i & 4 ? 1 : -1) * s.halfExtents.z};
      }
      for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit <= 4; bit *= 2) {
          if (!(i & bit)) {
            line(corners[i], corners[i | bit]);
          }
        }
      }
    } else {
      constexpr int kSegments = 48;
      for (int axis = 0; axis < 3; ++axis) {
        for (int i = 0; i < kSegments; ++i) {
          const auto point = [&](int segment) {
            const float angle =
                static_cast<float>(segment) * 6.28318530718f / kSegments;
            const float a = std::cos(angle) * s.radius,
                        b = std::sin(angle) * s.radius;
            Vec3 p = axis == 0   ? Vec3{0, a, b}
                     : axis == 1 ? Vec3{a, 0, b}
                                 : Vec3{a, b, 0};
            if (s.shape == ColliderShape::Capsule && axis != 1) {
              p.y += (p.y >= 0 ? s.halfHeight : -s.halfHeight);
            }
            return p;
          };
          if (s.shape == ColliderShape::Capsule && axis == 1) {
            const auto a = point(i), b = point(i + 1);
            line(a + Vec3{0, s.halfHeight, 0}, b + Vec3{0, s.halfHeight, 0});
            line(a + Vec3{0, -s.halfHeight, 0}, b + Vec3{0, -s.halfHeight, 0});
          } else {
            line(point(i), point(i + 1));
          }
        }
      }
    }
  }
}
} // namespace sat
