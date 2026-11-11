#pragma once
#include "Scene.hpp"
#include "SceneDraw.hpp"
#include <vector>
namespace sat {
// Appends camera-relative line segments; caller owns/reuses the output storage.
void appendColliderLines(const Entity &entity, DVec3 renderOrigin,
                         bool selected, std::vector<DebugLine> &lines);
} // namespace sat
