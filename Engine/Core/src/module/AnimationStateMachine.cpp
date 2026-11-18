#include "AnimationStateMachine.hpp"

namespace sat {
namespace {
bool passes(const AnimationCondition& condition, double value) {
    switch (condition.operation) {
    case AnimationConditionOperator::IsTrue: case AnimationConditionOperator::Triggered: return value != 0;
    case AnimationConditionOperator::IsFalse: return value == 0;
    case AnimationConditionOperator::Greater: return value > condition.value;
    case AnimationConditionOperator::Less: return value < condition.value;
    case AnimationConditionOperator::Equals: return value == condition.value;
    case AnimationConditionOperator::NotEquals: return value != condition.value;
    }
    return false;
}
}
int animationTransition(const AnimationGraph& graph, std::uint32_t state, double normalizedTime,
    std::span<const double> parameters, bool transitioning) {
    for (int priority = 0; priority < 2; ++priority) {
        for (std::size_t index = 0; index < graph.transitions.size(); ++index) {
            const auto& transition = graph.transitions[index];
            if (transition.source != (priority == 0 ? kAnimationAnyState : static_cast<int>(state)) ||
                ((transition.target == state || transitioning) && !transition.canInterruptSelf) ||
                (transition.hasExitTime && normalizedTime < transition.exitTime)) { continue; }
            bool valid = true;
            for (const auto& condition : transition.conditions) {
                if (!passes(condition, parameters[condition.parameter])) { valid = false; break; }
            }
            if (valid) { return static_cast<int>(index); }
        }
    }
    return -1;
}
void consumeAnimationTriggers(const AnimationGraph& graph, const AnimationTransition& transition, std::span<double> parameters) {
    for (const auto& condition : transition.conditions) {
        if (graph.parameters[condition.parameter].type == AnimationParameterType::Trigger) { parameters[condition.parameter] = 0; }
    }
}
} // namespace sat
