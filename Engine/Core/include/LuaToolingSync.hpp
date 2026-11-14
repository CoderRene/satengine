#pragma once

#include <filesystem>

namespace sat {

// Refreshes the engine-owned stubs and preserves other files in projectRoot/LuaDefs.
[[nodiscard]] bool syncProjectLuaDefinitions(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& luaDefsSource);

// Copies luaDefsSource → projectRoot/LuaDefs and writes .luarc.json, .vscode/settings.json,
// selene.toml, and satengine.yml for LuaLS + Selene in the project.
// luaDefsSource must contain sat.lua and globals.lua. Overwrites tooling config files.
[[nodiscard]] bool syncProjectLuaTooling(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& luaDefsSource);

} // namespace sat
