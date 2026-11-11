#pragma once
#include "Scene.hpp"
#include <span>
namespace sat {
// Sources and their transforms must remain valid for this sampling batch.
[[nodiscard]] DVec3 sampleGravity(std::span<const Entity *const> sources,
                                  DVec3 position);
} // namespace sat
