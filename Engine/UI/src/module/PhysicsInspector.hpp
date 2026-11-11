#pragma once
namespace sat {
struct Entity;
struct PhysicsSettings;
// Engine thread; returns whether authored data changed.
[[nodiscard]] bool drawPhysicsInspector(Entity &entity);
[[nodiscard]] bool drawPhysicsSettings(PhysicsSettings &settings);
} // namespace sat
