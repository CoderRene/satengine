#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
enum class AnimationParameterType { Bool, Float, Int, Trigger };
enum class AnimationConditionOperator { IsTrue, IsFalse, Greater, Less, Equals, NotEquals, Triggered };
inline constexpr int kAnimationAnyState = -1;
struct AnimationParameter {
    std::string name;
    AnimationParameterType type = AnimationParameterType::Bool;
    double value = 0; // Exact int32 values, booleans, or finite float values.
};
struct AnimationState {
    std::string name;
    std::string clip;
    float speed = 1;
    bool loop = true;
    float x = 0, y = 0;
};
struct AnimationCondition {
    std::uint32_t parameter = 0;
    AnimationConditionOperator operation = AnimationConditionOperator::IsTrue;
    double value = 0;
};
struct AnimationTransition {
    int source = kAnimationAnyState;
    std::uint32_t target = 0;
    float durationSeconds = 0.2f;
    bool hasExitTime = false;
    float exitTime = 1;
    bool canInterruptSelf = false;
    std::vector<AnimationCondition> conditions;
};
struct AnimationGraph {
    std::uint32_t defaultState = 0;
    std::vector<AnimationParameter> parameters;
    std::vector<AnimationState> states;
    std::vector<AnimationTransition> transitions;
};
[[nodiscard]] bool isAnimationGraphAssetPath(std::string_view path);
[[nodiscard]] bool validateAnimationGraph(const AnimationGraph& graph, std::string& error);
[[nodiscard]] bool encodeAnimationGraph(const AnimationGraph& graph, std::string& bytes, std::string& error);
[[nodiscard]] bool decodeAnimationGraph(std::string_view bytes, AnimationGraph& graph, std::string& error);
} // namespace sat
