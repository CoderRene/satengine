#include "OpenLuaFile.hpp"

#include <iostream>
#include <string>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace sat {
namespace {

std::wstring quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

} // namespace

bool openInCursor(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& filePath) {
    if (projectRoot.empty()) {
        return false;
    }

    // Prefer the multi-root workspace file (written by syncProjectLuaTooling). It embeds
    // Lua.workspace.library so reopening a script does not depend on hidden .satengine paths.
    // Falling back to the folder keeps older projects working before the next sync.
    const std::filesystem::path workspaceFile = projectRoot / "SatEngine.code-workspace";
    const std::filesystem::path workspace =
        std::filesystem::is_regular_file(workspaceFile) ? workspaceFile : projectRoot;

    //   cursor "<workspace>" --goto "<file>:1"
    std::wstring args = quote(workspace);
    if (!filePath.empty()) {
        args += L" --goto ";
        args += quote(filePath.wstring() + L":1");
    }

    // Requires the `cursor` CLI on PATH (Cursor installer: "Add to PATH").
    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        L"cursor",
        args.c_str(),
        nullptr,
        SW_SHOWNORMAL);

    if (reinterpret_cast<intptr_t>(result) <= 32) {
        std::cerr << "Failed to open Cursor (is the `cursor` CLI on PATH?)\n";
        return false;
    }
    return true;
}

} // namespace sat

#else

namespace sat {

bool openInCursor(
    const std::filesystem::path& /*projectRoot*/,
    const std::filesystem::path& /*filePath*/) {
    std::cerr << "openInCursor is only implemented on Windows.\n";
    return false;
}

} // namespace sat

#endif
