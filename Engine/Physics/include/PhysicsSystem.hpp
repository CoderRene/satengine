#pragma once

#include "Scene.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace sat {
using PhysicsFrameId = std::uint64_t;
struct PhysicsHit {
  EntityId entity = kInvalidEntityId;
  std::uint32_t colliderIndex = 0;
  DVec3 point{};
  Vec3 normal{};
  double distance = 0;
};
enum class PhysicsEventPhase { Enter, Stay, Exit };
struct PhysicsEvent {
  EntityId first = kInvalidEntityId;
  EntityId second = kInvalidEntityId;
  PhysicsEventPhase phase = PhysicsEventPhase::Enter;
  bool trigger = false;
};
struct PhysicsFrameState {
  PhysicsFrameId id = 0;
  DVec3 origin{};
  DVec3 velocity{};
  DVec3 acceleration{};
};
// Pure universe-space queries. Uniform acceleration is rotated by its source.
[[nodiscard]] DVec3 gravityAt(const Scene &scene, DVec3 position);
[[nodiscard]] std::optional<Vec3> upAt(const Scene &scene, DVec3 position);

// Engine-thread only. Borrows Scene for its entire lifetime. Worker listeners
// never access Scene or Lua. stop() joins work and invalidates all runtime body
// handles.
class PhysicsSystem {
public:
  explicit PhysicsSystem(Scene &scene);
  ~PhysicsSystem();
  PhysicsSystem(const PhysicsSystem &) = delete;
  PhysicsSystem &operator=(const PhysicsSystem &) = delete;
  PhysicsSystem(PhysicsSystem &&) = delete;
  PhysicsSystem &operator=(PhysicsSystem &&) = delete;
  [[nodiscard]] bool start();
  void stop();
  [[nodiscard]] bool running() const;
  [[nodiscard]] const std::string &error() const;
  [[nodiscard]] bool step(double seconds);
  [[nodiscard]] std::span<const PhysicsEvent> events() const;
  [[nodiscard]] std::size_t frameCount() const;
  [[nodiscard]] std::optional<PhysicsFrameState>
  frameFor(EntityId entity) const;
  [[nodiscard]] std::optional<DVec3> linearVelocity(EntityId entity) const;
  [[nodiscard]] std::optional<Vec3> angularVelocity(EntityId entity) const;
  [[nodiscard]] bool setLinearVelocity(EntityId entity, DVec3 velocity);
  [[nodiscard]] bool setAngularVelocity(EntityId entity, Vec3 velocity);
  [[nodiscard]] bool addForce(EntityId entity, Vec3 force);
  [[nodiscard]] bool addTorque(EntityId entity, Vec3 torque);
  [[nodiscard]] bool addImpulse(EntityId entity, Vec3 impulse);
  [[nodiscard]] bool addAngularImpulse(EntityId entity, Vec3 impulse);
  [[nodiscard]] bool teleport(EntityId entity, const Transform &transform,
                              bool clearVelocity = false);
  [[nodiscard]] bool moveKinematic(EntityId entity, DVec3 position,
                                   Vec3 rotation);
  [[nodiscard]] std::optional<PhysicsHit>
  raycast(DVec3 origin, Vec3 direction, double distance,
          EntityId ignore = kInvalidEntityId,
          bool includeTriggers = false) const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace sat
