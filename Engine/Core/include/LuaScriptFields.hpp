#pragma once

#include "ScriptFields.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sat {

// Discover an initial sat.expose declaration in an isolated, bounded Lua VM.
// Files with no declaration produce an empty set.
[[nodiscard]] bool inspectLuaScriptFields(const std::filesystem::path& scriptPath, ScriptFields& fields,
                                          std::string& error);

// Window-thread cache for one script file. Failed saves preserve the last valid
// preview and report the current error. Unchanged source is never recompiled.
class LuaScriptFieldPreview {
public:
    void refresh(const std::filesystem::path& scriptPath);
    [[nodiscard]] const ScriptFields& defaults() const { return m_defaults; }
    [[nodiscard]] const std::vector<std::string>& fieldOrder() const { return m_fieldOrder; }
    [[nodiscard]] const std::string& error() const { return m_error; }

private:
    std::optional<std::string> m_source;
    ScriptFields m_defaults;
    std::vector<std::string> m_fieldOrder;
    std::string m_error;
};

} // namespace sat
