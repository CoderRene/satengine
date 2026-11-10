#pragma once

#include <filesystem>

namespace sat {

// Copies luaDefsSource → projectRoot/LuaDefs and writes .luarc.json, .vscode/settings.json,
// SatEngine.code-workspace, selene.toml, and satengine.yml for LuaLS + Selene in the project.
// luaDefsSource must contain sat.lua (and usually globals.lua). Overwrites tooling files on each call.
[[nodiscard]] bool syncProjectLuaTooling(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& luaDefsSource);

} // namespace sat
