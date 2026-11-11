#pragma once

#include <iosfwd>
#include <string>

namespace sat {
struct Entity;
struct PhysicsSettings;
void writePhysicsSettings(std::ostream &out, const PhysicsSettings &settings);
[[nodiscard]] bool readPhysicsSettings(std::istream &in,
                                       PhysicsSettings &settings);
void writePhysicsComponents(std::ostream &out, const Entity &entity);
[[nodiscard]] bool readPhysicsComponents(std::istream &in, Entity &entity,
                                         std::string &error);
} // namespace sat
