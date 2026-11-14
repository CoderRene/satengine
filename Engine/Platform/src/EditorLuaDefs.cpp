#include "EditorLuaDefs.hpp"

#include "EnginePaths.hpp"

#include <iostream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sat {
namespace fs = std::filesystem;

namespace {

bool hasLuaDefinitions(const fs::path& directory) {
    std::error_code error;
    return fs::is_regular_file(directory / "sat.lua", error) &&
        fs::is_regular_file(directory / "globals.lua", error);
}

} // namespace

std::filesystem::path editorLuaDefsDirectory() {
    const fs::path configured(SAT_LUA_DEFS_DIR);
    if (hasLuaDefinitions(configured)) {
        return configured;
    }
#if defined(_WIN32)
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const fs::path besideExe = fs::path(modulePath).parent_path() / "LuaDefs";
        if (hasLuaDefinitions(besideExe)) {
            return besideExe;
        }
    }
#endif
    std::cerr << "editorLuaDefsDirectory: no LuaDefs beside the exe or at SAT_LUA_DEFS_DIR\n";
    return {};
}

} // namespace sat