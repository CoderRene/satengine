#include "AnimationStateMachine.hpp"

#include <initializer_list>

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
AnimationTransitionLists indexAnimationTransitions(const AnimationGraph& graph) {
    AnimationTransitionLists lists;
    lists.byState.resize(graph.states.size());
    for (std::uint32_t i = 0; i < graph.transitions.size(); ++i) {
        const auto source = graph.transitions[i].source;
        auto& indices = source == kAnimationAnyState ? lists.anyState : lists.byState[source];
        indices.push_back(i);
    }
    return lists;
}
int animationTransition(const AnimationGraph& graph, const AnimationTransitionLists& lists, std::uint32_t state,
    double normalizedTime, std::span<const double> parameters, bool transitioning) {
    for (const auto* transitions : {&lists.anyState, &lists.byState[state]}) {
        for (const auto index : *transitions) {
            const auto& transition = graph.transitions[index];
            if (((transition.target == state || transitioning) && !transition.canInterruptSelf) ||
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
