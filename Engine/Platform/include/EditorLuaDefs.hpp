#pragma once

#include <filesystem>

namespace sat {

// Uses the source tree when available, then LuaDefs beside the packaged editor.
[[nodiscard]] std::filesystem::path editorLuaDefsDirectory();

} // namespace sat