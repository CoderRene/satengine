#include "LuaToolingSync.hpp"

#include <fstream>
#include <iostream>

namespace sat {
namespace fs = std::filesystem;

namespace {

// Non-hidden LuaDefs/ so LuaLS keeps definitions after the script tab is closed.
// Dot-folders / gitignore matches can make stubs apply only while a defs file is open.
constexpr const char* kLuarc = R"json({
  "$schema": "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json",
  "runtime": {
    "version": "Lua 5.4"
  },
  "workspace": {
    "library": [
      "LuaDefs"
    ],
    "checkThirdParty": false,
    "useGitIgnore": false
  },
  "diagnostics": {
    "globals": [
      "sat",
      "self",
      "onStart",
      "onUpdate",
      "onStop"
    ]
  }
}
)json";

constexpr const char* kVscodeSettings = R"json({
  "Lua.runtime.version": "Lua 5.4",
  "Lua.workspace.library": [
    "${workspaceFolder}/LuaDefs"
  ],
  "Lua.workspace.checkThirdParty": false,
  "Lua.workspace.useGitIgnore": false,
  "Lua.diagnostics.globals": [
    "sat",
    "self",
    "onStart",
    "onUpdate",
    "onStop"
  ],
  "selene.configPath": "${workspaceFolder}/selene.toml"
}
)json";

constexpr const char* kCodeWorkspace = R"json({
  "folders": [
    {
      "path": "."
    }
  ],
  "settings": {
    "Lua.runtime.version": "Lua 5.4",
    "Lua.workspace.library": [
      "${workspaceFolder}/LuaDefs"
    ],
    "Lua.workspace.checkThirdParty": false,
    "Lua.workspace.useGitIgnore": false,
    "Lua.diagnostics.globals": [
      "sat",
      "self",
      "onStart",
      "onUpdate",
      "onStop"
    ],
    "selene.configPath": "${workspaceFolder}/selene.toml"
  }
}
)json";

// Project-local Selene config (must live in the opened game project, not only the engine repo).
constexpr const char* kSeleneToml = R"toml(std = "satengine"
# EmmyLua stubs are for LuaLS only; empty bodies trip unused_variable.
exclude = ["LuaDefs/**"]
)toml";

constexpr const char* kSatengineYml = R"yml(# Selene std overlay for SatEngine script globals.
# Winget Selene builds ship lua51 as the only base std; LuaLS remains Lua 5.4.
base: lua51
name: satengine
globals:
  sat:
    any: true
  self:
    any: true
  onStart:
    any: true
  onUpdate:
    any: true
  onStop:
    any: true
)yml";

bool writeTextFile(const fs::path& path, const char* text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << text;
    out.close();
    return static_cast<bool>(out);
}

bool copyTree(const fs::path& from, const fs::path& to, std::error_code& ec) {
    fs::remove_all(to, ec);
    ec.clear();
    fs::create_directories(to, ec);
    if (ec) {
        return false;
    }
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return !ec;
}

} // namespace

bool syncProjectLuaTooling(const fs::path& projectRoot, const fs::path& luaDefsSource) {
    if (projectRoot.empty() || !fs::is_directory(projectRoot)) {
        std::cerr << "syncProjectLuaTooling: invalid project root\n";
        return false;
    }
    if (!fs::is_directory(luaDefsSource) || !fs::is_regular_file(luaDefsSource / "sat.lua")) {
        std::cerr << "syncProjectLuaTooling: LuaDefs not found at " << luaDefsSource << '\n';
        return false;
    }

    std::error_code ec;
    // Drop the old hidden location; LuaLS often treats it like an ignored path.
    fs::remove_all(projectRoot / ".satengine", ec);
    ec.clear();

    const fs::path dest = projectRoot / "LuaDefs";
    if (!copyTree(luaDefsSource, dest, ec)) {
        std::cerr << "syncProjectLuaTooling: failed to copy LuaDefs: " << ec.message() << '\n';
        return false;
    }

    fs::create_directories(projectRoot / ".vscode", ec);
    if (ec) {
        std::cerr << "syncProjectLuaTooling: failed to create .vscode: " << ec.message() << '\n';
        return false;
    }

    if (!writeTextFile(projectRoot / ".luarc.json", kLuarc) ||
        !writeTextFile(projectRoot / ".vscode" / "settings.json", kVscodeSettings) ||
        !writeTextFile(projectRoot / "SatEngine.code-workspace", kCodeWorkspace) ||
        !writeTextFile(projectRoot / "selene.toml", kSeleneToml) ||
        !writeTextFile(projectRoot / "satengine.yml", kSatengineYml)) {
        std::cerr << "syncProjectLuaTooling: failed to write Lua tooling config files\n";
        return false;
    }
    return true;
}

} // namespace sat
