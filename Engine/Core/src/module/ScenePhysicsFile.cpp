#include "ScenePhysicsFile.hpp"

#include "Scene.hpp"

#include <cmath>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>

namespace sat {
namespace {
template <class V> bool finite(const V &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
template <class V> void writeVector(std::ostream &out, const V &v) {
  out << v.x << ' ' << v.y << ' ' << v.z << ' ';
}
template <class V> bool readVector(std::istream &in, V &v) {
  return static_cast<bool>(in >> v.x >> v.y >> v.z) && finite(v);
}
bool positive(double value) { return std::isfinite(value) && value > 0; }
bool nonnegative(double value) { return std::isfinite(value) && value >= 0; }
} // namespace
void writePhysicsSettings(std::ostream &out, const PhysicsSettings &s) {
  out << std::setprecision(std::numeric_limits<double>::max_digits10)
      << s.fixedSeconds << ' ' << s.maxCatchUpSteps << ' '
      << s.interactionDistance << ' ' << s.rebaseDistance << ' '
      << s.maxRelativeSpeed << ' ' << s.maxBodies << ' ' << s.maxContactEvents
      << '\n';
}
bool readPhysicsSettings(std::istream &in, PhysicsSettings &s) {
  return static_cast<bool>(in >> s.fixedSeconds >> s.maxCatchUpSteps >>
                           s.interactionDistance >> s.rebaseDistance >>
                           s.maxRelativeSpeed >> s.maxBodies >>
                           s.maxContactEvents) &&
         positive(s.fixedSeconds) && s.fixedSeconds <= 1 &&
         s.maxCatchUpSteps > 0 && s.maxCatchUpSteps <= 64 &&
         positive(s.interactionDistance) && positive(s.rebaseDistance) &&
         positive(s.maxRelativeSpeed) &&
         s.maxRelativeSpeed <= kMaxPhysicsRelativeSpeed && s.maxBodies > 0 &&
         s.maxBodies <= 100000 && s.maxContactEvents > 0 &&
         s.maxContactEvents <= 1048576;
}
void writePhysicsComponents(std::ostream &out, const Entity &e) {
  const auto &b = e.rigidBody;
  const auto &c = e.collider;
  const auto &g = e.gravitySource;
  out << std::setprecision(std::numeric_limits<double>::max_digits10)
      << e.hasRigidBody << ' ' << static_cast<int>(b.motion) << ' ' << b.mass
      << ' ' << b.gravityFactor << ' ' << b.linearDamping << ' '
      << b.angularDamping << ' ' << b.allowSleeping << ' ' << b.continuous
      << ' ' << static_cast<unsigned>(b.allowedDofs) << ' ';
  writeVector(out, b.linearVelocity);
  writeVector(out, b.angularVelocity);
  out << '\n'
      << e.hasCollider << ' ' << c.trigger << ' ' << c.friction << ' '
      << c.restitution << ' ' << c.shapes.size() << '\n';
  for (const auto &shape : c.shapes) {
    out << static_cast<int>(shape.shape) << ' ';
    writeVector(out, shape.offset);
    writeVector(out, shape.rotation);
    writeVector(out, shape.halfExtents);
    out << shape.radius << ' ' << shape.halfHeight << '\n';
  }
  out << e.hasGravitySource << ' ' << static_cast<int>(g.mode) << ' ' << g.mu
      << ' ' << g.minimumRadius << ' ';
  writeVector(out, g.acceleration);
  out << g.influenceRadius << '\n';
}
bool readPhysicsComponents(std::istream &in, Entity &e, std::string &error) {
  const auto fail = [&] {
    error = "Invalid physics components on entity " + e.name;
    return false;
  };
  auto &b = e.rigidBody;
  auto &c = e.collider;
  auto &g = e.gravitySource;
  int motion = 0, mode = 0;
  unsigned dofs = 0;
  std::size_t count = 0;
  if (!(in >> e.hasRigidBody >> motion >> b.mass >> b.gravityFactor >>
        b.linearDamping >> b.angularDamping >> b.allowSleeping >>
        b.continuous >> dofs) ||
      motion < 0 || motion > 2 || dofs > 63 || !positive(b.mass) ||
      !nonnegative(b.gravityFactor) || !nonnegative(b.linearDamping) ||
      !nonnegative(b.angularDamping) || !readVector(in, b.linearVelocity) ||
      !readVector(in, b.angularVelocity)) {
    return fail();
  }
  b.motion = static_cast<BodyMotion>(motion);
  b.allowedDofs = static_cast<std::uint8_t>(dofs);
  if (!(in >> e.hasCollider >> c.trigger >> c.friction >> c.restitution >>
        count) ||
      count > 64 || (e.hasCollider && count == 0) || !nonnegative(c.friction) ||
      !nonnegative(c.restitution) || c.restitution > 1) {
    return fail();
  }
  if (e.hasCollider && (!positive(e.transform.scale.x) ||
                        e.transform.scale.x != e.transform.scale.y ||
                        e.transform.scale.x != e.transform.scale.z)) {
    error = "Collider requires positive uniform scale on entity " + e.name;
    return false;
  }
  c.shapes.resize(count);
  for (auto &s : c.shapes) {
    int shape = 0;
    if (!(in >> shape) || shape < 0 || shape > 2 || !readVector(in, s.offset) ||
        !readVector(in, s.rotation) || !readVector(in, s.halfExtents) ||
        !(in >> s.radius >> s.halfHeight) || !positive(s.halfExtents.x) ||
        !positive(s.halfExtents.y) || !positive(s.halfExtents.z) ||
        !positive(s.radius) || !positive(s.halfHeight)) {
      return fail();
    }
    s.shape = static_cast<ColliderShape>(shape);
  }
  if (!(in >> e.hasGravitySource >> mode >> g.mu >> g.minimumRadius) ||
      mode < 0 || mode > 1 || !readVector(in, g.acceleration) ||
      !(in >> g.influenceRadius) || !nonnegative(g.mu) ||
      !positive(g.minimumRadius) || !nonnegative(g.influenceRadius)) {
    return fail();
  }
  g.mode = static_cast<GravityMode>(mode);
  return true;
}
} // namespace sat
