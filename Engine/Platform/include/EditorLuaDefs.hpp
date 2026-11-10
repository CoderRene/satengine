#pragma once

#include <filesystem>

namespace sat {

// Prefers <SatEngine.exe dir>/LuaDefs (POST_BUILD copy), else SAT_LUA_DEFS_DIR.
[[nodiscard]] std::filesystem::path editorLuaDefsDirectory();

} // namespace sat