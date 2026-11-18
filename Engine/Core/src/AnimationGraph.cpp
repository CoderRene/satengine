#include "AnimationGraph.hpp"

#include "AssetData.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace sat {
namespace {
bool validName(const std::string& name) {
    return !name.empty() && name.size() <= 4096 && name.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool validValue(AnimationParameterType type, double value) {
    if (!std::isfinite(value)) { return false; }
    switch (type) {
    case AnimationParameterType::Bool: case AnimationParameterType::Trigger: return value == 0 || value == 1;
    case AnimationParameterType::Int: return value >= INT32_MIN && value <= INT32_MAX && std::floor(value) == value;
    case AnimationParameterType::Float: return std::abs(value) <= std::numeric_limits<float>::max();
    }
    return false;
}
}
bool isAnimationGraphAssetPath(std::string_view path) {
    if (!isValidAssetPath(path) || path.size() < 11) { return false; }
    auto extension = std::string(path.substr(path.size() - 11));
    for (char& ch : extension) { if (ch >= 'A' && ch <= 'Z') { ch += 'a' - 'A'; } }
    return extension == ".satanimfsm";
}
bool validateAnimationGraph(const AnimationGraph& graph, std::string& error) {
    const auto fail = [&](const char* message) { error = message; return false; };
    if (graph.states.empty() || graph.states.size() > 4096 || graph.parameters.size() > 256 ||
        graph.transitions.size() > 16384 || graph.defaultState >= graph.states.size()) { return fail("Invalid graph counts or default state"); }
    std::unordered_set<std::string> names;
    for (const auto& parameter : graph.parameters) {
        if (!validName(parameter.name) || !names.insert(parameter.name).second || !validValue(parameter.type, parameter.value)) {
            return fail("Invalid or duplicate animation parameter");
        }
    }
    names.clear();
    for (const auto& state : graph.states) {
        if (!validName(state.name) || !names.insert(state.name).second || !validName(state.clip) ||
            !std::isfinite(state.speed) || !std::isfinite(state.x) || !std::isfinite(state.y)) { return fail("Invalid or duplicate animation state"); }
    }
    for (const auto& transition : graph.transitions) {
        if (transition.source < -1 || transition.source >= static_cast<int>(graph.states.size()) || transition.target >= graph.states.size() ||
            !std::isfinite(transition.durationSeconds) || transition.durationSeconds < 0 ||
            !std::isfinite(transition.exitTime) || transition.exitTime < 0 || transition.conditions.size() > 256) { return fail("Invalid animation transition"); }
        for (const auto& condition : transition.conditions) {
            if (condition.parameter >= graph.parameters.size() || !std::isfinite(condition.value)) { return fail("Invalid condition parameter"); }
            const auto type = graph.parameters[condition.parameter].type;
            const auto operation = condition.operation;
            const bool valid = type == AnimationParameterType::Trigger ? operation == AnimationConditionOperator::Triggered :
                type == AnimationParameterType::Bool ? operation == AnimationConditionOperator::IsTrue || operation == AnimationConditionOperator::IsFalse :
                operation >= AnimationConditionOperator::Greater && operation <= AnimationConditionOperator::NotEquals && validValue(type, condition.value);
            if (!valid) { return fail("Condition operator does not match parameter type"); }
        }
    }
    error.clear();
    return true;
}
bool encodeAnimationGraph(const AnimationGraph& graph, std::string& bytes, std::string& error) {
    if (!validateAnimationGraph(graph, error)) { return false; }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "SatAnimationGraph 1\ndefault " << graph.defaultState << '\n';
    for (const auto& parameter : graph.parameters) {
        out << "parameter " << std::quoted(parameter.name) << ' ' << static_cast<int>(parameter.type) << ' ' << parameter.value << '\n';
    }
    for (const auto& state : graph.states) {
        out << "state " << std::quoted(state.name) << ' ' << std::quoted(state.clip) << ' ' << state.speed << ' ' << state.loop << ' ' << state.x << ' ' << state.y << '\n';
    }
    for (const auto& transition : graph.transitions) {
        out << "transition " << transition.source << ' ' << transition.target << ' ' << transition.durationSeconds << ' '
            << transition.hasExitTime << ' ' << transition.exitTime << ' ' << transition.canInterruptSelf << '\n';
        for (const auto& condition : transition.conditions) {
            out << "condition " << condition.parameter << ' ' << static_cast<int>(condition.operation) << ' ' << condition.value << '\n';
        }
    }
    auto result = out.str();
    if (result.size() > kMaxAssetBytes) { error = "Animation graph exceeds asset limit"; return false; }
    bytes = std::move(result);
    return true;
}
bool decodeAnimationGraph(std::string_view bytes, AnimationGraph& graph, std::string& error) {
    if (bytes.size() > kMaxAssetBytes) { error = "Line 1: graph exceeds asset limit"; return false; }
    std::istringstream input{std::string(bytes)};
    input.imbue(std::locale::classic());
    AnimationGraph candidate;
    std::string line;
    std::size_t lineNumber = 0;
    bool header = false, defaultSeen = false;
    const auto fail = [&](std::string message) { error = "Line " + std::to_string(lineNumber) + ": " + message; return false; };
    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string token;
        if (!(row >> token) || token.starts_with('#')) { continue; }
        if (!header) {
            int version = 0;
            if (token != "SatAnimationGraph" || !(row >> version) || version != 1) { return fail("Invalid graph header"); }
            header = true;
        } else if (token == "default") {
            if (defaultSeen || !(row >> candidate.defaultState)) { return fail("Invalid default state"); }
            defaultSeen = true;
        } else if (token == "parameter") {
            AnimationParameter parameter;
            int type = 0;
            if (candidate.parameters.size() >= 256 || !(row >> std::quoted(parameter.name) >> type >> parameter.value) || type < 0 || type > 3) { return fail("Invalid parameter"); }
            parameter.type = static_cast<AnimationParameterType>(type);
            candidate.parameters.push_back(std::move(parameter));
        } else if (token == "state") {
            AnimationState state;
            if (candidate.states.size() >= 4096 || !(row >> std::quoted(state.name) >> std::quoted(state.clip) >> state.speed >> state.loop >> state.x >> state.y)) { return fail("Invalid state"); }
            candidate.states.push_back(std::move(state));
        } else if (token == "transition") {
            AnimationTransition transition;
            if (candidate.transitions.size() >= 16384 || !(row >> transition.source >> transition.target >> transition.durationSeconds >> transition.hasExitTime >> transition.exitTime >> transition.canInterruptSelf)) { return fail("Invalid transition"); }
            candidate.transitions.push_back(std::move(transition));
        } else if (token == "condition") {
            AnimationCondition condition;
            int operation = 0;
            if (candidate.transitions.empty() || candidate.transitions.back().conditions.size() >= 256 ||
                !(row >> condition.parameter >> operation >> condition.value) || operation < 0 || operation > 6) { return fail("Invalid condition"); }
            condition.operation = static_cast<AnimationConditionOperator>(operation);
            candidate.transitions.back().conditions.push_back(condition);
        } else { return fail("Unknown graph field " + token); }
        row >> std::ws;
        if (!row.eof()) { return fail("Unexpected trailing field"); }
    }
    if (!header || !defaultSeen) { return fail("Missing graph header or default state"); }
    std::string validation;
    if (!validateAnimationGraph(candidate, validation)) { return fail(validation); }
    graph = std::move(candidate);
    error.clear();
    return true;
}
} // namespace sat
