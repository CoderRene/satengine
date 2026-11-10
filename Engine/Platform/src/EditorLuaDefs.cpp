#include "EditorLuaDefs.hpp"

#include "EnginePaths.hpp"

#include <iostream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sat {
namespace fs = std::filesystem;

std::filesystem::path editorLuaDefsDirectory() {
#if defined(_WIN32)
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const fs::path besideExe = fs::path(modulePath).parent_path() / "LuaDefs";
        if (fs::is_regular_file(besideExe / "sat.lua")) {
            return besideExe;
        }
    }
#endif
    const fs::path configured(SAT_LUA_DEFS_DIR);
    if (fs::is_regular_file(configured / "sat.lua")) {
        return configured;
    }
    std::cerr << "editorLuaDefsDirectory: no LuaDefs beside the exe or at SAT_LUA_DEFS_DIR\n";
    return {};
}

} // namespace sat