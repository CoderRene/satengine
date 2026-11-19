#pragma once
#include <iosfwd>
#include <string>
namespace sat {
struct Entity;
void writeAnimationComponent(std::ostream& out, const Entity& entity, bool hasOcclusionRadius);
[[nodiscard]] bool readAnimationComponent(std::istream& in, Entity& entity, bool hasModes, bool hasOcclusionRadius, std::string& error);
}
