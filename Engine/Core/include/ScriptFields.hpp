#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace sat {

struct ScriptEntityReference {
    std::uint64_t entityId = 0;

    bool operator==(const ScriptEntityReference&) const = default;
};

using ScriptFieldValue = std::variant<bool, std::int64_t, double, std::string, ScriptEntityReference>;
using ScriptFields = std::map<std::string, ScriptFieldValue, std::less<>>;

inline constexpr std::size_t kMaxScriptFields = 128;
inline constexpr std::size_t kMaxScriptFieldNameBytes = 128;
inline constexpr std::size_t kMaxScriptFieldTextBytes = 4096;

inline bool isValidScriptFieldName(std::string_view name) {
    if (name.empty() || name.size() > kMaxScriptFieldNameBytes || name == "entity") {
        return false;
    }
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!letter(name.front())) {
        return false;
    }
    for (const char c : name.substr(1)) {
        if (!letter(c) && (c < '0' || c > '9')) {
            return false;
        }
    }
    return true;
}

inline bool isValidScriptFieldText(std::string_view value) {
    return value.size() <= kMaxScriptFieldTextBytes && value.find_first_of("\r\n\0", 0, 3) == std::string_view::npos;
}

} // namespace sat
