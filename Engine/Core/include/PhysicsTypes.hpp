#pragma once

#include "Math.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sat {

enum class BodyMotion { Static, Dynamic, Kinematic };
enum class ColliderShape { Box, Sphere, Capsule };
enum class GravityMode { Spherical, Uniform };

struct ColliderPrimitive {
    ColliderShape shape = ColliderShape::Box;
    Vec3 offset{};
    Vec3 rotation{}; // Euler degrees, Rz * Ry * Rx.
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float halfHeight = 0.5f; // Capsule cylinder half-height, local Y axis.

    bool operator==(const ColliderPrimitive&) const = default;
};

struct ColliderComponent {
    std::vector<ColliderPrimitive> shapes{ColliderPrimitive{}};
    bool trigger = false;
    float friction = 0.5f;
    float restitution = 0.0f;

    bool operator==(const ColliderComponent&) const = default;
};

struct RigidBodyComponent {
    BodyMotion motion = BodyMotion::Dynamic;
    float mass = 1.0f;
    float gravityFactor = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    bool allowSleeping = true;
    bool continuous = false;
    // Bits 0..2 permit world-frame XYZ translation, bits 3..5 rotation.
    std::uint8_t allowedDofs = 63;
    DVec3 linearVelocity{}; // Initial universe velocity, meters/second.
    Vec3 angularVelocity{}; // Initial world angular velocity, radians/second.

    bool operator==(const RigidBodyComponent&) const = default;
};

struct GravitySourceComponent {
    GravityMode mode = GravityMode::Spherical;
    double mu = 9.81; // Gravitational parameter, m^3/s^2.
    double minimumRadius = 1.0;
    Vec3 acceleration{0.0f, -9.81f, 0.0f}; // Source-local uniform acceleration.
    double influenceRadius = 0.0; // Uniform fields only; zero means global.

    bool operator==(const GravitySourceComponent&) const = default;
};

// Relative speed only; universe velocity is unrestricted by this solver guard.
// Leaves headroom for squaring Jolt's higher internal speed limit in float math.
inline constexpr double kMaxPhysicsRelativeSpeed = 1.0e15;

struct PhysicsSettings {
    double fixedSeconds = 1.0 / 60.0;
    std::uint32_t maxCatchUpSteps = 8;
    double interactionDistance = 2000.0;
    double rebaseDistance = 1000.0;
    double maxRelativeSpeed = 1000.0;
    std::uint32_t maxBodies = 65536;
    std::uint32_t maxContactEvents = 262144;
};

} // namespace sat
