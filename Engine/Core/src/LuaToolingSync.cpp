#include "LuaToolingSync.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>

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

bool updateDefinition(const fs::path& source, const fs::path& destination) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string contents{std::istreambuf_iterator<char>(input), {}};
    if (input.bad() || contents.empty()) {
        return false;
    }
    {
        std::ifstream previous(destination, std::ios::binary);
        if (previous) {
            const std::string existing{std::istreambuf_iterator<char>(previous), {}};
            if (!previous.bad() && existing == contents) {
                return true;
            }
        }
    }
    // Keep the current definition in place if the write fails.
    auto staged = destination;
    staged += ".tmp";
    std::error_code error;
    if (!writeTextFile(staged, contents.c_str())) {
        fs::remove(staged, error);
        return false;
    }
    fs::rename(staged, destination, error);
    if (error) {
        fs::remove(staged, error);
        return false;
    }
    return true;
}

} // namespace

bool syncProjectLuaDefinitions(const fs::path& projectRoot, const fs::path& luaDefsSource) {
    std::error_code ec;
    if (projectRoot.empty() || !fs::is_directory(projectRoot, ec)) {
        std::cerr << "syncProjectLuaDefinitions: invalid project root " << projectRoot << '\n';
        return false;
    }
    const auto destination = projectRoot / "LuaDefs";
    fs::create_directories(destination, ec);
    if (ec) {
        std::cerr << "Cannot create " << destination << ": " << ec.message() << '\n';
        return false;
    }
    for (const char* name : {"sat.lua", "globals.lua"}) {
        const auto source = luaDefsSource / name;
        const auto target = destination / name;
        if (luaDefsSource.empty() || !updateDefinition(source, target)) {
            std::cerr << "Cannot update " << target << " from " << source << '\n';
            return false;
        }
    }
    return true;
}

bool syncProjectLuaTooling(const fs::path& projectRoot, const fs::path& luaDefsSource) {
    if (!syncProjectLuaDefinitions(projectRoot, luaDefsSource)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(projectRoot / ".vscode", ec);
    if (ec) {
        std::cerr << "syncProjectLuaTooling: failed to create .vscode: " << ec.message() << '\n';
        return false;
    }

    if (!writeTextFile(projectRoot / ".luarc.json", kLuarc) ||
        !writeTextFile(projectRoot / ".vscode" / "settings.json", kVscodeSettings) ||
        !writeTextFile(projectRoot / "selene.toml", kSeleneToml) ||
        !writeTextFile(projectRoot / "satengine.yml", kSatengineYml)) {
        std::cerr << "syncProjectLuaTooling: failed to write Lua tooling config files\n";
        return false;
    }
    return true;
}

} // namespace sat
