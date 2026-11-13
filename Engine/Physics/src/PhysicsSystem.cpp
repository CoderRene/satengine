#include "PhysicsSystem.hpp"
#include "module/Gravity.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sat {
namespace {
JPH::Vec3 vector(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::Vec3 vector(DVec3 v) {
  return {static_cast<float>(v.x), static_cast<float>(v.y),
          static_cast<float>(v.z)};
}
JPH::RVec3 position(DVec3 v) { return {v.x, v.y, v.z}; }
DVec3 universe(JPH::RVec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Vec3 local(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
double length(DVec3 v) { return std::hypot(v.x, v.y, v.z); }
bool finite(DVec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool finite(Vec3 v) { return finite(DVec3{v.x, v.y, v.z}); }
JPH::Quat quaternion(Vec3 degrees) {
  const auto x =
      JPH::Quat::sRotation(JPH::Vec3::sAxisX(), degrees.x * kDegToRad);
  const auto y =
      JPH::Quat::sRotation(JPH::Vec3::sAxisY(), degrees.y * kDegToRad);
  const auto z =
      JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), degrees.z * kDegToRad);
  return z * y * x;
}
Vec3 euler(JPH::QuatArg q) {
  // Extract Rz * Ry * Rx, including the gimbal-lock branch.
  const auto m = JPH::Mat44::sRotation(q);
  const float y = std::asin(std::clamp(-m(2, 0), -1.0f, 1.0f));
  const bool regular = std::abs(std::cos(y)) > 1e-6f;
  return {
      (regular ? std::atan2(m(2, 1), m(2, 2)) : std::atan2(-m(1, 2), m(1, 1))) /
          kDegToRad,
      y / kDegToRad,
      (regular ? std::atan2(m(1, 0), m(0, 0)) : 0.0f) / kDegToRad};
}
void trace(const char *format, ...) {
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
}
#ifdef JPH_ENABLE_ASSERTS
bool assertFailed(const char *expression, const char *message, const char *file,
                  JPH::uint line) {
  std::fprintf(stderr, "Jolt assertion: %s (%s) at %s:%u\n", expression,
               message ? message : "", file, line);
  return true;
}
#endif
struct JoltLifetime {
  JoltLifetime() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = trace;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = assertFailed;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
  ~JoltLifetime() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
};
std::shared_ptr<JoltLifetime> acquireJolt() {
  static std::mutex mutex;
  static std::weak_ptr<JoltLifetime> weak;
  const std::lock_guard lock(mutex);
  auto lifetime = weak.lock();
  if (!lifetime) {
    lifetime = std::make_shared<JoltLifetime>();
    weak = lifetime;
  }
  return lifetime;
}
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
public:
  JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
  JPH::BroadPhaseLayer
  GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return JPH::BroadPhaseLayer(layer);
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *
  GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer.GetValue() == 0 ? "Static" : "Moving";
  }
#endif
};
class ObjectLayers final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a != 0 || b != 0;
  }
};
class ObjectBroadLayers final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a,
                     JPH::BroadPhaseLayer b) const override {
    return a != 0 || b.GetValue() != 0;
  }
};
using Pair = std::tuple<EntityId, EntityId, bool>;
struct Contact {
  EntityId a, b;
  std::uint32_t sa, sb;
  bool trigger;
  auto key() const { return std::tuple{a, b, sa, sb, trigger}; }
  bool operator<(const Contact &other) const { return key() < other.key(); }
};
struct ContactChange {
  Contact contact;
  bool added;
};
struct Listener final : JPH::ContactListener {
  std::mutex mutex;
  std::unordered_map<JPH::uint32, EntityId> identities;
  std::vector<ContactChange> changes;
  std::set<Contact> active;
  std::size_t capacity = 0;
  bool overflow = false;
  Contact contact(JPH::BodyID a, JPH::BodyID b, JPH::SubShapeID sa,
                  JPH::SubShapeID sb, bool trigger) const {
    const auto ia = identities.find(a.GetIndexAndSequenceNumber());
    const auto ib = identities.find(b.GetIndexAndSequenceNumber());
    return {ia == identities.end() ? 0 : ia->second,
            ib == identities.end() ? 0 : ib->second, sa.GetValue(),
            sb.GetValue(), trigger};
  }
  void record(Contact c, bool added) {
    const std::lock_guard lock(mutex);
    if (changes.size() == capacity) {
      overflow = true;
      return;
    }
    changes.push_back({c, added});
  }
  void OnContactAdded(const JPH::Body &a, const JPH::Body &b,
                      const JPH::ContactManifold &m,
                      JPH::ContactSettings &) override {
    record(contact(a.GetID(), b.GetID(), m.mSubShapeID1, m.mSubShapeID2,
                   a.IsSensor() || b.IsSensor()),
           true);
  }
  void OnContactRemoved(const JPH::SubShapeIDPair &p) override {
    // Removed callbacks have no Body access. Resolve immutable per-step IDs.
    record(contact(p.GetBody1ID(), p.GetBody2ID(), p.GetSubShapeID1(),
                   p.GetSubShapeID2(), false),
           false);
  }
  bool collect(std::vector<Pair> &pairs, std::size_t pairCapacity) {
    for (const auto &change : changes) {
      if (change.added) {
        active.insert(change.contact);
      } else {
        auto c = change.contact;
        active.erase(c);
        c.trigger = true;
        active.erase(c);
      }
    }
    changes.clear();
    if (active.size() > capacity ||
        active.size() > pairCapacity - pairs.size()) {
      return false;
    }
    for (const auto &c : active) {
      pairs.emplace_back(std::min(c.a, c.b), std::max(c.a, c.b), c.trigger);
    }
    return true;
  }
  void remove(EntityId id) {
    std::erase_if(active,
                  [id](const Contact &c) { return c.a == id || c.b == id; });
  }
};
struct World {
  PhysicsFrameState state;
  BroadLayers broad;
  ObjectLayers objects;
  ObjectBroadLayers objectBroad;
  Listener listener;
  JPH::PhysicsSystem system;
  double separatedSeconds = 0;
  std::size_t members = 0;
  std::size_t scratchIndex = 0;
  explicit World(const PhysicsSettings &settings, std::size_t bodyCount) {
    listener.capacity = std::min<std::size_t>(
        settings.maxContactEvents, std::max<std::size_t>(1024, bodyCount * 32));
    listener.changes.reserve(listener.capacity);
    system.Init(static_cast<JPH::uint>(bodyCount), 0,
                static_cast<JPH::uint>(listener.capacity),
                static_cast<JPH::uint>(listener.capacity), broad, objectBroad,
                objects);
    system.SetGravity(JPH::Vec3::sZero());
    system.SetContactListener(&listener);
  }
};
struct Body {
  EntityId entity = 0;
  World *world = nullptr;
  JPH::BodyID id;
  JPH::RefConst<JPH::Shape> shape;
  DVec3 velocity{};
  Vec3 angular{};
  Transform synced;
  BodyMotion motion = BodyMotion::Static;
  float radius = 0;
  Vec3 force{}, torque{};
  DVec3 lastAcceleration{};
  std::optional<Transform> target;
  bool sleeping = false;
  bool added = false;
  bool velocityDirty = false;
  bool restoreSleep = false;
};
} // namespace

struct PhysicsSystem::Impl {
  explicit Impl(Scene &scene) : scene(scene) {}
  Scene &scene;
  PhysicsSettings settings;
  std::shared_ptr<JoltLifetime> lifetime;
  std::unique_ptr<JPH::TempAllocatorImplWithMallocFallback> allocator;
  std::unique_ptr<JPH::JobSystemThreadPool> jobs;
  std::vector<std::unique_ptr<World>> worlds;
  std::vector<Body> bodies;
  std::vector<const Entity *> gravitySources;
  std::unordered_map<EntityId, std::size_t> indices;
  std::vector<std::size_t> order, parents;
  struct WorldBounds {
    double radius = 0;
    double speed = 0;
  };
  std::vector<WorldBounds> worldBounds;
  std::vector<Pair> previousPairs, currentPairs;
  std::vector<PhysicsEvent> events;
  PhysicsFrameId nextFrame = 1;
  std::string error;
  bool running = false;
  bool fail(std::string message) {
    error = std::move(message);
    running = false;
    return false;
  }
  Body *find(EntityId id) {
    const auto it = indices.find(id);
    return it == indices.end() ? nullptr : &bodies[it->second];
  }
  bool dynamic(Body *b) {
    return b && b->motion == BodyMotion::Dynamic
               ? true
               : fail("Physics operation requires a dynamic body");
  }
  void remove(Body &b) {
    if (!b.world) {
      return;
    }
    auto &world = *b.world;
    if (b.added) {
      world.system.GetBodyInterface().RemoveBody(b.id);
    }
    world.system.GetBodyInterface().DestroyBody(b.id);
    world.listener.identities.erase(b.id.GetIndexAndSequenceNumber());
    world.listener.remove(b.entity);
    --world.members;
    b.world = nullptr;
    b.added = false;
  }
  bool buildShape(Body &b, const Entity &e) {
    JPH::StaticCompoundShapeSettings compound;
    const float scale = e.transform.scale.x;
    if (!finite(e.transform.position) || !finite(e.transform.rotation) ||
        !std::isfinite(scale) || scale <= 0 || scale != e.transform.scale.y ||
        scale != e.transform.scale.z || e.collider.shapes.empty() ||
        e.collider.shapes.size() > 64) {
      return fail("Invalid collider transform or shape count: " + e.name);
    }
    std::uint64_t colliderIndex = 0;
    for (const auto &primitive : e.collider.shapes) {
      JPH::ShapeSettings::ShapeResult result;
      if (primitive.shape == ColliderShape::Box) {
        const Vec3 extents = primitive.halfExtents * scale;
        if (!finite(extents) || extents.x <= 0 || extents.y <= 0 ||
            extents.z <= 0) {
          return fail("Invalid box: " + e.name);
        }
        result =
            JPH::BoxShapeSettings(
                vector(extents), std::min({0.05f, extents.x * 0.1f,
                                           extents.y * 0.1f, extents.z * 0.1f}))
                .Create();
      } else if (primitive.shape == ColliderShape::Sphere) {
        if (!std::isfinite(primitive.radius * scale) || primitive.radius <= 0) {
          return fail("Invalid sphere: " + e.name);
        }
        result = JPH::SphereShapeSettings(primitive.radius * scale).Create();
      } else if (primitive.shape == ColliderShape::Capsule) {
        if (!std::isfinite((primitive.radius + primitive.halfHeight) * scale) ||
            primitive.radius <= 0 || primitive.halfHeight <= 0) {
          return fail("Invalid capsule: " + e.name);
        }
        result = JPH::CapsuleShapeSettings(primitive.halfHeight * scale,
                                           primitive.radius * scale)
                     .Create();
      } else {
        return fail("Unknown collider shape: " + e.name);
      }
      if (result.HasError()) {
        return fail(e.name + ": " + result.GetError().c_str());
      }
      if (!finite(primitive.offset * scale) || !finite(primitive.rotation)) {
        return fail("Invalid collider offset: " + e.name);
      }
      result.Get()->SetUserData(colliderIndex++);
      compound.AddShape(vector(primitive.offset * scale),
                        quaternion(primitive.rotation), result.Get());
    }
    auto result = compound.Create();
    if (result.HasError()) {
      return fail(e.name + ": " + result.GetError().c_str());
    }
    b.shape = result.Get();
    const auto bounds = b.shape->GetLocalBounds();
    b.radius = std::max(bounds.mMin.Length(), bounds.mMax.Length());
    if (!std::isfinite(b.radius) || b.radius > settings.interactionDistance) {
      return fail("Collider exceeds local interaction distance: " + e.name);
    }
    return true;
  }
  bool configureBody(Body &b, const Entity &e) {
    b.entity = e.id();
    b.synced = e.transform;
    b.motion = e.hasRigidBody ? e.rigidBody.motion : BodyMotion::Static;
    b.velocity =
        b.motion == BodyMotion::Static ? DVec3{} : e.rigidBody.linearVelocity;
    b.angular = e.rigidBody.angularVelocity;
    if (!finite(b.velocity) || !finite(b.angular) ||
        !std::isfinite(e.rigidBody.mass) || e.rigidBody.mass <= 0 ||
        !std::isfinite(e.rigidBody.gravityFactor) ||
        e.rigidBody.gravityFactor < 0 ||
        !std::isfinite(e.rigidBody.linearDamping) ||
        e.rigidBody.linearDamping < 0 ||
        !std::isfinite(e.rigidBody.angularDamping) ||
        e.rigidBody.angularDamping < 0 ||
        (b.motion != BodyMotion::Static && b.motion != BodyMotion::Dynamic &&
         b.motion != BodyMotion::Kinematic) ||
        !std::isfinite(e.collider.friction) || e.collider.friction < 0 ||
        !std::isfinite(e.collider.restitution) ||
        e.collider.restitution < 0 || e.collider.restitution > 1 ||
        e.rigidBody.allowedDofs > 63) {
      return fail("Invalid body settings: " + e.name);
    }
    if (!buildShape(b, e)) {
      return false;
    }
    return true;
  }
  bool create(Body &b, World &world) {
    const auto *e = scene.entityById(b.entity);
    const auto &rb = e->rigidBody;
    JPH::EMotionType motion = JPH::EMotionType::Static;
    if (b.motion == BodyMotion::Dynamic) {
      motion = JPH::EMotionType::Dynamic;
    }
    if (b.motion == BodyMotion::Kinematic) {
      motion = JPH::EMotionType::Kinematic;
    }
    JPH::BodyCreationSettings info(
        b.shape, position(e->transform.position - world.state.origin),
        quaternion(e->transform.rotation), motion,
        motion == JPH::EMotionType::Static && !e->collider.trigger ? 0 : 1);
    info.mUserData = b.entity;
    info.mIsSensor = e->collider.trigger;
    info.mCollideKinematicVsNonDynamic = true;
    info.mFriction = e->collider.friction;
    info.mRestitution = e->collider.restitution;
    info.mGravityFactor = 0;
    info.mAllowSleeping = rb.allowSleeping;
    info.mLinearDamping = rb.linearDamping;
    info.mAngularDamping = rb.angularDamping;
    info.mMotionQuality = rb.continuous ? JPH::EMotionQuality::LinearCast
                                        : JPH::EMotionQuality::Discrete;
    info.mAllowedDOFs = static_cast<JPH::EAllowedDOFs>(rb.allowedDofs);
    info.mOverrideMassProperties =
        JPH::EOverrideMassProperties::CalculateInertia;
    info.mMassPropertiesOverride.mMass = rb.mass;
    const DVec3 relativeVelocity = b.velocity - world.state.velocity;
    if (!finite(relativeVelocity) || length(relativeVelocity) > settings.maxRelativeSpeed) {
      return fail("Physics frame relative speed exceeded: " + e->name);
    }
    info.mLinearVelocity = vector(relativeVelocity);
    info.mAngularVelocity = vector(b.angular);
    // Jolt squares this limit when clamping velocity. float::max() overflows
    // there and traps in Windows builds with floating-point exceptions enabled.
    // Keep the solver guard above our supported speed so excess motion is still
    // reported by the engine's post-step validation rather than silently hidden.
    info.mMaxLinearVelocity = static_cast<float>(std::max(1.0, settings.maxRelativeSpeed * 2.0));
    auto &api = world.system.GetBodyInterface();
    auto *body = api.CreateBody(info);
    if (!body) {
      return fail("Jolt body capacity exhausted for " + e->name);
    }
    b.id = body->GetID();
    b.world = &world;
    b.velocityDirty = false;
    ++world.members;
    world.listener.identities.emplace(b.id.GetIndexAndSequenceNumber(),
                                      b.entity);
    // Transfer sleeping bodies awake for one step so contacts are rebuilt.
    b.restoreSleep = b.sleeping;
    // The owning group adds all created bodies in one broadphase batch.
    return true;
  }
  std::size_t root(std::size_t i) {
    while (parents[i] != i) {
      parents[i] = parents[parents[i]];
      i = parents[i];
    }
    return i;
  }
  void join(std::size_t a, std::size_t b) {
    a = root(a);
    b = root(b);
    if (a != b) {
      parents[std::max(a, b)] = std::min(a, b);
    }
  }
  bool reconcile(double dt) {
    for (std::size_t i = 0; i < bodies.size();) {
      if (!scene.entityById(bodies[i].entity)) {
        remove(bodies[i]);
        indices.erase(bodies[i].entity);
        if (i + 1 != bodies.size()) {
          bodies[i] = std::move(bodies.back());
          indices[bodies[i].entity] = i;
        }
        bodies.pop_back();
      } else {
        ++i;
      }
    }
    const auto count = bodies.size();
    // Reuse compact, separated established worlds without allocating or
    // rebuilding groups. Bounds include next-tick relative motion.
    std::erase_if(worlds,
                  [](const auto &world) { return world->members == 0; });
    worldBounds.assign(worlds.size(), {});
    for (std::size_t i = 0; i < worlds.size(); ++i) {
      worlds[i]->scratchIndex = i;
    }
    bool stable = !worlds.empty();
    for (const auto &body : bodies) {
      if (!body.world) {
        stable = false;
        continue;
      }
      const auto &e = *scene.entityById(body.entity);
      auto &bounds = worldBounds[body.world->scratchIndex];
      bounds.radius = std::max(bounds.radius, length(e.transform.position -
                                                     body.world->state.origin) +
                                                  body.radius);
      bounds.speed = std::max(
          bounds.speed, length(body.velocity - body.world->state.velocity));
      stable = stable && bounds.radius < settings.interactionDistance &&
               bounds.speed <= settings.maxRelativeSpeed;
    }
    if (stable) {
      for (std::size_t i = 0; i < worlds.size() && stable; ++i) {
        for (std::size_t j = i + 1; j < worlds.size(); ++j) {
          const auto &a = worlds[i]->state;
          const auto &b = worlds[j]->state;
          const double swept = (length(a.velocity - b.velocity) +
                                worldBounds[i].speed + worldBounds[j].speed) *
                               dt;
          if (length(a.origin - b.origin) <=
              worldBounds[i].radius + worldBounds[j].radius +
                  settings.interactionDistance + swept) {
            stable = false;
            break;
          }
        }
      }
      if (stable) {
        return true;
      }
    }
    order.resize(count);
    parents.resize(count);
    std::iota(order.begin(), order.end(), 0);
    std::iota(parents.begin(), parents.end(), 0);
    const auto minX = [&](std::size_t i) {
      const auto &b = bodies[i];
      return scene.entityById(b.entity)->transform.position.x - b.radius -
             settings.interactionDistance - std::abs(b.velocity.x) * dt;
    };
    std::sort(order.begin(), order.end(),
              [&](auto a, auto b) { return minX(a) < minX(b); });
    for (std::size_t x = 0; x < count; ++x) {
      const auto i = order[x];
      const auto &a = bodies[i];
      const auto pa = scene.entityById(a.entity)->transform.position;
      for (std::size_t y = x + 1; y < count; ++y) {
        const auto j = order[y];
        const auto &b = bodies[j];
        const auto pb = scene.entityById(b.entity)->transform.position;
        const bool same = a.world && a.world == b.world;
        const double padding = settings.interactionDistance * (same ? 2 : 1);
        const double sweep = length(a.velocity - b.velocity) * dt;
        if (minX(j) > pa.x + a.radius + settings.interactionDistance * 3 +
                          std::abs(a.velocity.x) * dt) {
          break;
        }
        if (length(pa - pb) <= a.radius + b.radius + padding + sweep) {
          join(i, j);
        }
      }
    }
    // Delay splits; merging is immediate and always precedes the solver.
    for (const auto &world : worlds) {
      std::optional<std::size_t> first;
      bool split = false;
      for (std::size_t i = 0; i < count; ++i) {
        if (bodies[i].world == world.get()) {
          if (!first) {
            first = i;
          } else if (root(i) != root(*first)) {
            split = true;
          }
        }
      }
      world->separatedSeconds = split ? world->separatedSeconds + dt : 0;
      if (split && world->separatedSeconds < 1) {
        for (std::size_t i = 0; i < count; ++i) {
          if (bodies[i].world == world.get()) {
            join(i, *first);
          }
        }
      }
    }
    // Plan target worlds before moving bodies; raw World pointers remain
    // stable.
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < count; ++i) {
      groups[root(i)].push_back(i);
    }
    std::set<World *> claimed;
    for (const auto &[key, members] : groups) {
      (void)key;
      World *target = bodies[members.front()].world;
      bool unchanged = target && !claimed.contains(target) &&
                       target->members == members.size();
      for (auto i : members) {
        unchanged = unchanged && bodies[i].world == target;
        if (target && length(bodies[i].velocity - target->state.velocity) >
                          settings.maxRelativeSpeed * 0.75) {
          unchanged = false;
        }
      }
      if (!unchanged) {
        auto world = std::make_unique<World>(settings, members.size());
        world->state.id = nextFrame++;
        DVec3 center{}, velocity{};
        double weight = 0;
        bool anchored = false;
        const DVec3 base = scene.entityById(bodies[members.front()].entity)
                               ->transform.position;
        for (auto i : members) {
          const auto &b = bodies[i];
          const auto &e = *scene.entityById(b.entity);
          const double mass =
              b.motion == BodyMotion::Dynamic ? e.rigidBody.mass : 1;
          center = center + (e.transform.position - base) * mass;
          velocity = velocity + b.velocity * mass;
          weight += mass;
          anchored = anchored || b.motion == BodyMotion::Static;
        }
        world->state.origin = base + center * (1 / weight);
        world->state.velocity = anchored ? DVec3{} : velocity * (1 / weight);
        target = world.get();
        worlds.push_back(std::move(world));
        std::vector<JPH::BodyID> batch;
        batch.reserve(members.size());
        for (auto i : members) {
          remove(bodies[i]);
          if (!create(bodies[i], *target)) {
            return false;
          }
          batch.push_back(bodies[i].id);
        }
        auto &api = target->system.GetBodyInterface();
        const auto addState =
            api.AddBodiesPrepare(batch.data(), static_cast<int>(batch.size()));
        api.AddBodiesFinalize(batch.data(), static_cast<int>(batch.size()),
                              addState, JPH::EActivation::Activate);
        for (auto i : members) {
          bodies[i].added = true;
        }
      }
      claimed.insert(target);
      for (auto i : members) {
        auto &b = bodies[i];
        const auto &e = *scene.entityById(b.entity);
        if (length(b.velocity - target->state.velocity) >
                settings.maxRelativeSpeed ||
            length(e.transform.position - target->state.origin) >
                settings.interactionDistance * 8) {
          return fail("Physics frame extent or relative speed exceeded: " +
                      e.name);
        }
      }
    }
    std::erase_if(worlds, [](const auto &w) { return w->members == 0; });
    return true;
  }
};
PhysicsSystem::PhysicsSystem(Scene &scene)
    : m_impl(std::make_unique<Impl>(scene)) {}
PhysicsSystem::~PhysicsSystem() { stop(); }
bool PhysicsSystem::start() {
  stop();
  auto &s = *m_impl;
  s.error.clear();
  s.settings = s.scene.physicsSettings;
  const auto initialize = [&]() -> bool {
    if (!(s.settings.fixedSeconds > 0) ||
        !std::isfinite(s.settings.fixedSeconds) ||
        s.settings.fixedSeconds > 1 || s.settings.maxCatchUpSteps == 0 ||
        s.settings.maxCatchUpSteps > 64 ||
        !(s.settings.interactionDistance > 0) ||
        !std::isfinite(s.settings.interactionDistance) ||
        !(s.settings.rebaseDistance > 0) ||
        !std::isfinite(s.settings.rebaseDistance) ||
        !(s.settings.maxRelativeSpeed > 0) ||
        !std::isfinite(s.settings.maxRelativeSpeed) ||
        s.settings.maxRelativeSpeed > kMaxPhysicsRelativeSpeed ||
        s.settings.maxBodies == 0 || s.settings.maxBodies > 100000 ||
        s.settings.maxContactEvents == 0 ||
        s.settings.maxContactEvents > 1048576) {
      return s.fail("Invalid physics settings");
    }
    s.lifetime = acquireJolt();
    s.allocator = std::make_unique<JPH::TempAllocatorImplWithMallocFallback>(
        32 * 1024 * 1024);
    const unsigned threads = std::thread::hardware_concurrency();
    s.jobs = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>(threads > 1 ? std::min(threads - 1, 8u) : 0));
    for (const auto &e : s.scene.entities()) {
      if (e.hasGravitySource) {
        const auto &g = e.gravitySource;
        if (!finite(e.transform.position) || !finite(e.transform.rotation) ||
            !finite(g.acceleration) || !std::isfinite(g.mu) || g.mu < 0 ||
            !std::isfinite(g.minimumRadius) || g.minimumRadius <= 0 ||
            !std::isfinite(g.influenceRadius) || g.influenceRadius < 0 ||
            (g.mode != GravityMode::Spherical &&
             g.mode != GravityMode::Uniform)) {
          return s.fail("Invalid gravity source: " + e.name);
        }
      }
      if (!e.hasCollider) {
        continue;
      }
      if (s.bodies.size() == s.settings.maxBodies) {
        return s.fail("Physics body capacity exceeded");
      }
      Body b;
      if (!s.configureBody(b, e)) {
        return false;
      }
      s.indices.emplace(e.id(), s.bodies.size());
      s.bodies.push_back(std::move(b));
    }
    s.events.reserve(s.settings.maxContactEvents);
    s.previousPairs.reserve(s.settings.maxContactEvents);
    s.currentPairs.reserve(s.settings.maxContactEvents);
    s.running = true;
    return s.reconcile(s.settings.fixedSeconds);
  };
  try {
    if (initialize()) {
      return true;
    }
  } catch (const std::exception &exception) {
    s.fail(std::string("Physics startup failed: ") + exception.what());
  }
  stop();
  return false;
}

void PhysicsSystem::stop() {
  auto &s = *m_impl;
  for (auto &b : s.bodies) {
    s.remove(b);
  }
  s.bodies.clear();
  s.gravitySources.clear();
  s.indices.clear();
  s.worlds.clear();
  s.events.clear();
  s.previousPairs.clear();
  s.currentPairs.clear();
  s.jobs.reset();
  s.allocator.reset();
  s.lifetime.reset();
  s.running = false;
  s.nextFrame = 1;
}
bool PhysicsSystem::running() const { return m_impl->running; }
const std::string &PhysicsSystem::error() const { return m_impl->error; }
std::span<const PhysicsEvent> PhysicsSystem::events() const {
  return m_impl->events;
}
std::size_t PhysicsSystem::frameCount() const { return m_impl->worlds.size(); }
std::optional<PhysicsFrameState> PhysicsSystem::frameFor(EntityId id) const {
  auto *b = m_impl->find(id);
  return b && b->world ? std::optional{b->world->state} : std::nullopt;
}
std::optional<DVec3> PhysicsSystem::linearVelocity(EntityId id) const {
  auto *b = m_impl->find(id);
  return b ? std::optional{b->velocity} : std::nullopt;
}
std::optional<Vec3> PhysicsSystem::angularVelocity(EntityId id) const {
  auto *b = m_impl->find(id);
  return b ? std::optional{b->angular} : std::nullopt;
}
bool PhysicsSystem::setLinearVelocity(EntityId id, DVec3 v) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  if (!b || b->motion == BodyMotion::Static || !finite(v)) {
    return s.fail("Velocity requires a movable body and finite vector");
  }
  b->velocity = v;
  b->velocityDirty = true;
  b->sleeping = false;
  return true;
}
bool PhysicsSystem::setAngularVelocity(EntityId id, Vec3 v) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  if (!b || b->motion == BodyMotion::Static || !finite(v)) {
    return s.fail("Angular velocity requires a movable body and finite vector");
  }
  b->angular = v;
  if (b->world) {
    b->world->system.GetBodyInterface().SetAngularVelocity(b->id, vector(v));
  }
  return true;
}
bool PhysicsSystem::addForce(EntityId id, Vec3 v) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  if (!s.dynamic(b)) {
    return false;
  }
  if (!finite(v)) {
    return s.fail("Invalid force");
  }
  b->force = b->force + v;
  return true;
}
bool PhysicsSystem::addTorque(EntityId id, Vec3 v) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  if (!s.dynamic(b)) {
    return false;
  }
  if (!finite(v)) {
    return s.fail("Invalid torque");
  }
  b->torque = b->torque + v;
  return true;
}
bool PhysicsSystem::addImpulse(EntityId id, Vec3 impulse) {
  auto &state = *m_impl;
  auto *body = state.find(id);
  if (!state.dynamic(body)) {
    return false;
  }
  if (!finite(impulse)) {
    return state.fail("Invalid impulse");
  }
  const auto &settings = state.scene.entityById(id)->rigidBody;
  DVec3 delta{impulse.x / static_cast<double>(settings.mass),
              impulse.y / static_cast<double>(settings.mass),
              impulse.z / static_cast<double>(settings.mass)};
  if (!(settings.allowedDofs & 1)) {
    delta.x = 0;
  }
  if (!(settings.allowedDofs & 2)) {
    delta.y = 0;
  }
  if (!(settings.allowedDofs & 4)) {
    delta.z = 0;
  }
  return setLinearVelocity(id, body->velocity + delta);
}

bool PhysicsSystem::addAngularImpulse(EntityId id, Vec3 v) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  if (!s.dynamic(b)) {
    return false;
  }
  if (!finite(v)) {
    return s.fail("Invalid angular impulse");
  }
  auto &api = b->world->system.GetBodyInterface();
  api.AddAngularImpulse(b->id, vector(v));
  b->angular = local(api.GetAngularVelocity(b->id));
  return true;
}
bool PhysicsSystem::teleport(EntityId id, const Transform &t, bool clear) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  auto *e = s.scene.entityById(id);
  if (!b || !e || !finite(t.position) || !finite(t.rotation)) {
    return s.fail("Teleport requires a valid body and finite transform");
  }
  e->transform.position = t.position;
  e->transform.rotation = t.rotation;
  b->synced = e->transform;
  auto &api = b->world->system.GetBodyInterface();
  api.SetPositionAndRotation(
      b->id, position(t.position - b->world->state.origin),
      quaternion(t.rotation), JPH::EActivation::Activate);
  if (clear && b->motion != BodyMotion::Static) {
    b->velocity = {};
    b->angular = {};
    api.SetLinearAndAngularVelocity(
        b->id, vector(DVec3{} - b->world->state.velocity), JPH::Vec3::sZero());
  }
  return true;
}
bool PhysicsSystem::moveKinematic(EntityId id, DVec3 p, Vec3 r) {
  auto &s = *m_impl;
  auto *b = s.find(id);
  if (!b || b->motion != BodyMotion::Kinematic || !finite(p) || !finite(r)) {
    return s.fail(
        "Kinematic target requires a kinematic body and finite transform");
  }
  Transform t = b->synced;
  t.position = p;
  t.rotation = r;
  b->target = t;
  return true;
}
bool PhysicsSystem::refreshBody(EntityId id, bool resetLinearVelocity,
                                bool resetAngularVelocity) {
  auto &s = *m_impl;
  const auto *entity = s.scene.entityById(id);
  if (!s.running || !entity) {
    return false;
  }

  const auto existing = s.indices.find(id);
  const bool hasBody = existing != s.indices.end();
  if (!entity->hasCollider) {
    if (hasBody) {
      const auto index = existing->second;
      s.remove(s.bodies[index]);
      s.indices.erase(existing);
      if (index + 1 != s.bodies.size()) {
        s.bodies[index] = std::move(s.bodies.back());
        s.indices.at(s.bodies[index].entity) = index;
      }
      s.bodies.pop_back();
    }
    return true;
  }
  if (!hasBody && s.bodies.size() == s.settings.maxBodies) {
    s.error = "Physics body capacity exceeded";
    return false;
  }

  Body replacement;
  if (!s.configureBody(replacement, *entity)) {
    // Invalid editor input leaves the existing body running.
    s.running = true;
    return false;
  }
  if (hasBody) {
    auto &body = s.bodies[existing->second];
    if (replacement.motion != BodyMotion::Static && body.motion != BodyMotion::Static) {
      if (!resetLinearVelocity) { replacement.velocity = body.velocity; }
      if (!resetAngularVelocity) { replacement.angular = body.angular; }
    }
    s.remove(body);
    body = std::move(replacement);
  } else {
    const auto inserted = s.indices.emplace(id, s.bodies.size()).first;
    try {
      s.bodies.push_back(std::move(replacement));
    } catch (...) {
      s.indices.erase(inserted);
      throw;
    }
  }
  return s.reconcile(s.settings.fixedSeconds);
}
bool PhysicsSystem::step(double dt) {
  auto &s = *m_impl;
  if (!s.running) {
    return false;
  }
  const auto simulate = [&]() -> bool {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1) {
      return s.fail("Invalid physics timestep");
    }
    // Existing script transform setters write Scene fields; detect explicit
    // changes.
    for (auto &b : s.bodies) {
      const auto *e = s.scene.entityById(b.entity);
      if (!e) {
        continue;
      }
      if (length(e->transform.position - b.synced.position) > 0 ||
          e->transform.rotation.x != b.synced.rotation.x ||
          e->transform.rotation.y != b.synced.rotation.y ||
          e->transform.rotation.z != b.synced.rotation.z) {
        if (!teleport(b.entity, e->transform)) {
          return false;
        }
      }
      if (b.target) {
        b.velocity = (b.target->position - e->transform.position) * (1 / dt);
      }
    }
    if (!s.reconcile(dt)) {
      return false;
    }
    s.gravitySources.clear();
    for (const auto &e : s.scene.entities()) {
      if (e.hasGravitySource) {
        s.gravitySources.push_back(&e);
      }
    }
    for (auto &b : s.bodies) {
      auto &world = *b.world;
      auto &api = world.system.GetBodyInterface();
      const auto &e = *s.scene.entityById(b.entity);
      if (b.velocityDirty) {
        api.SetLinearVelocity(b.id, vector(b.velocity - world.state.velocity));
        b.velocityDirty = false;
      }
      if (b.motion == BodyMotion::Dynamic) {
        const DVec3 acceleration =
            sampleGravity(s.gravitySources, e.transform.position) *
                e.rigidBody.gravityFactor -
            world.state.acceleration;
        const DVec3 force = acceleration * e.rigidBody.mass +
                            DVec3{b.force.x, b.force.y, b.force.z};
        if (!finite(force) || !finite(b.torque) ||
            length(force) > std::numeric_limits<float>::max()) {
          return s.fail("Physics force overflow: " + e.name);
        }
        const auto activation =
            (length(DVec3{b.force.x, b.force.y, b.force.z}) > 0 ||
             length(DVec3{b.torque.x, b.torque.y, b.torque.z}) > 0 ||
             (acceleration.x != b.lastAcceleration.x ||
              acceleration.y != b.lastAcceleration.y ||
              acceleration.z != b.lastAcceleration.z))
                ? JPH::EActivation::Activate
                : JPH::EActivation::DontActivate;
        api.AddForceAndTorque(b.id, vector(force), vector(b.torque),
                              activation);
        b.lastAcceleration = acceleration;
      } else if (b.motion == BodyMotion::Kinematic && b.target) {
        api.MoveKinematic(
            b.id,
            position(b.target->position -
                     (world.state.origin + world.state.velocity * dt)),
            quaternion(b.target->rotation), static_cast<float>(dt));
      }
      b.force = {};
      b.torque = {};
    }
    s.currentPairs.clear();
    s.events.clear();
    for (auto &world : s.worlds) {
      const auto result = world->system.Update(static_cast<float>(dt), 1,
                                               s.allocator.get(), s.jobs.get());
      if (result != JPH::EPhysicsUpdateError::None ||
          world->listener.overflow) {
        return s.fail("Jolt contact/pair capacity exhausted");
      }
      world->state.origin = world->state.origin + world->state.velocity * dt;
      if (!world->listener.collect(s.currentPairs,
                                   s.settings.maxContactEvents)) {
        return s.fail("Physics contact capacity exhausted");
      }
    }
    for (auto &b : s.bodies) {
      auto &world = *b.world;
      auto &api = world.system.GetBodyInterface();
      auto &e = *s.scene.entityById(b.entity);
      if (b.motion != BodyMotion::Static) {
        JPH::RVec3 p;
        JPH::Quat q;
        api.GetPositionAndRotation(b.id, p, q);
        e.transform.position = world.state.origin + universe(p);
        e.transform.rotation = euler(q);
        b.velocity = world.state.velocity +
                     universe(JPH::RVec3(api.GetLinearVelocity(b.id)));
        b.angular = local(api.GetAngularVelocity(b.id));
        if (!finite(e.transform.position) || !finite(b.velocity) ||
            length(b.velocity - world.state.velocity) >
                s.settings.maxRelativeSpeed * 1.0001) {
          return s.fail("Physics state exceeded supported limits: " + e.name);
        }
        if (b.restoreSleep) {
          if (length(b.velocity - world.state.velocity) < 1e-6 &&
              length(DVec3{b.angular.x, b.angular.y, b.angular.z}) < 1e-6) {
            api.DeactivateBody(b.id);
          }
          b.restoreSleep = false;
        }
        b.sleeping = !api.IsActive(b.id);
      }
      if (b.motion == BodyMotion::Kinematic && b.target) {
        b.velocity = {};
        b.angular = {};
        api.SetLinearAndAngularVelocity(
            b.id, vector(DVec3{} - world.state.velocity), JPH::Vec3::sZero());
      }
      b.synced = e.transform;
      b.target.reset();
    }
    for (auto &world : s.worlds) {
      DVec3 center{};
      std::size_t count = 0;
      for (const auto &b : s.bodies) {
        if (b.world == world.get()) {
          center = center + (s.scene.entityById(b.entity)->transform.position -
                             world->state.origin);
          ++count;
        }
      }
      if (count) {
        center = center * (1.0 / static_cast<double>(count));
      }
      if (length(center) > s.settings.rebaseDistance) {
        for (auto &body : s.bodies) {
          if (body.world != world.get()) {
            continue;
          }
          auto &api = world->system.GetBodyInterface();
          api.SetPosition(body.id, api.GetPosition(body.id) - position(center),
                          JPH::EActivation::DontActivate);
        }
        world->state.origin = world->state.origin + center;
      }
    }
    std::sort(s.currentPairs.begin(), s.currentPairs.end());
    s.currentPairs.erase(
        std::unique(s.currentPairs.begin(), s.currentPairs.end()),
        s.currentPairs.end());
    const auto emit = [&](const Pair &pair, PhysicsEventPhase phase) {
      if (s.events.size() == s.settings.maxContactEvents) {
        return s.fail("Physics event capacity exhausted");
      }
      s.events.push_back(
          {std::get<0>(pair), std::get<1>(pair), phase, std::get<2>(pair)});
      return true;
    };
    for (const auto &pair : s.currentPairs) {
      if (!emit(pair, std::binary_search(s.previousPairs.begin(),
                                         s.previousPairs.end(), pair)
                          ? PhysicsEventPhase::Stay
                          : PhysicsEventPhase::Enter)) {
        return false;
      }
    }
    for (const auto &pair : s.previousPairs) {
      if (!std::binary_search(s.currentPairs.begin(), s.currentPairs.end(),
                              pair) &&
          !emit(pair, PhysicsEventPhase::Exit)) {
        return false;
      }
    }
    std::sort(s.events.begin(), s.events.end(),
              [](const auto &a, const auto &b) {
                return std::tuple{a.first, a.second, a.trigger, a.phase} <
                       std::tuple{b.first, b.second, b.trigger, b.phase};
              });
    s.previousPairs.swap(s.currentPairs);
    return true;
  };
  try {
    return simulate();
  } catch (const std::exception &exception) {
    return s.fail(std::string("Physics step failed: ") + exception.what());
  }
}

std::optional<PhysicsHit> PhysicsSystem::raycast(DVec3 origin, Vec3 direction,
                                                 double distance,
                                                 EntityId ignore,
                                                 bool triggers) const {
  if (!finite(origin) || !finite(direction) || !std::isfinite(distance) ||
      distance <= 0) {
    return std::nullopt;
  }
  const float magnitude = std::hypot(direction.x, direction.y, direction.z);
  if (magnitude <= 0) {
    return std::nullopt;
  }
  direction = direction * (1 / magnitude);
  struct Filter final : JPH::BodyFilter {
    EntityId ignore;
    bool triggers;
    Filter(EntityId id, bool include) : ignore(id), triggers(include) {}
    bool ShouldCollideLocked(const JPH::Body &body) const override {
      return body.GetUserData() != ignore && (triggers || !body.IsSensor());
    }
  } filter(ignore, triggers);
  std::optional<PhysicsHit> closest;
  // Segment long rays to keep float ray displacement within each world's
  // bounds.
  for (const auto &world : m_impl->worlds) {
    JPH::AABox bounds;
    bool any = false;
    for (const auto &b : m_impl->bodies) {
      if (b.world == world.get()) {
        auto box = b.shape->GetLocalBounds();
        const auto p = m_impl->scene.entityById(b.entity)->transform.position -
                       world->state.origin;
        const auto center = vector(p);
        const auto extent = JPH::Vec3::sReplicate(b.radius);
        box = JPH::AABox(center - extent, center + extent);
        if (!any) {
          bounds = box;
          any = true;
        } else {
          bounds.Encapsulate(box);
        }
      }
    }
    if (!any) {
      continue;
    }
    const DVec3 relative = origin - world->state.origin;
    double begin = 0, end = closest ? closest->distance : distance;
    const double o[3] = {relative.x, relative.y, relative.z},
                 d[3] = {direction.x, direction.y, direction.z};
    for (int axis = 0; axis < 3; ++axis) {
      if (std::abs(d[axis]) < 1e-12) {
        if (o[axis] < bounds.mMin[axis] || o[axis] > bounds.mMax[axis]) {
          end = -1;
        }
      } else {
        double a = (bounds.mMin[axis] - o[axis]) / d[axis],
               b = (bounds.mMax[axis] - o[axis]) / d[axis];
        if (a > b) {
          std::swap(a, b);
        }
        begin = std::max(begin, a);
        end = std::min(end, b);
      }
    }
    if (end <= begin) {
      continue;
    }
    const JPH::RRayCast ray(
        position(relative +
                 DVec3{direction.x, direction.y, direction.z} * begin),
        vector(direction) * static_cast<float>(end - begin));
    JPH::RayCastResult hit;
    if (!world->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {},
                                                     filter)) {
      continue;
    }
    const double hitDistance = begin + (end - begin) * hit.mFraction;
    JPH::BodyLockRead lock(world->system.GetBodyLockInterface(), hit.mBodyID);
    if (!lock.Succeeded()) {
      continue;
    }
    const auto &body = lock.GetBody();
    closest = PhysicsHit{
        body.GetUserData(),
        static_cast<std::uint32_t>(
            body.GetShape()->GetSubShapeUserData(hit.mSubShapeID2)),
        origin + DVec3{direction.x, direction.y, direction.z} * hitDistance,
        (hit.mFraction <= 1e-6f
             ? direction * -1.0f
             : local(body.GetWorldSpaceSurfaceNormal(
                   hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)))),
        hitDistance};
  }
  return closest;
}
} // namespace sat
