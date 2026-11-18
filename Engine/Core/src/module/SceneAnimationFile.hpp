#pragma once
#include <iosfwd>
#include <string>
namespace sat {
struct Entity;
void writeAnimationComponent(std::ostream& out, const Entity& entity);
[[nodiscard]] bool readAnimationComponent(std::istream& in, Entity& entity, std::string& error);
}
