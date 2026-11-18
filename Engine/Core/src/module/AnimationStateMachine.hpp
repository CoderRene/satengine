#pragma once
#include "AnimationGraph.hpp"
#include <span>

namespace sat {
// Returns a transition index, with Any State priority and stable file ordering.
[[nodiscard]] int animationTransition(const AnimationGraph& graph, std::uint32_t state,
    double normalizedTime, std::span<const double> parameters, bool transitioning);
void consumeAnimationTriggers(const AnimationGraph& graph, const AnimationTransition& transition, std::span<double> parameters);
} // namespace sat
