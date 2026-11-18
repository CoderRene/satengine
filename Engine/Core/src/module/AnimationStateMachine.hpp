#pragma once
#include "AnimationGraph.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace sat {
struct AnimationTransitionLists {
    std::vector<std::uint32_t> anyState;
    std::vector<std::vector<std::uint32_t>> byState;
};
[[nodiscard]] AnimationTransitionLists indexAnimationTransitions(const AnimationGraph& graph);
// Returns a transition index, with Any State priority and stable file ordering.
[[nodiscard]] int animationTransition(const AnimationGraph& graph, const AnimationTransitionLists& lists, std::uint32_t state,
    double normalizedTime, std::span<const double> parameters, bool transitioning);
void consumeAnimationTriggers(const AnimationGraph& graph, const AnimationTransition& transition, std::span<double> parameters);
} // namespace sat
