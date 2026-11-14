#include "OpenLuaFile.hpp"

#include "EditorLuaDefs.hpp"
#include "LuaToolingSync.hpp"

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

    const auto definitions = editorLuaDefsDirectory();
    if (definitions.empty() || !syncProjectLuaDefinitions(projectRoot, definitions)) {
        std::cerr << "Could not refresh Lua definitions before opening Cursor for " << projectRoot << '\n';
    }

    // Open the folder so Cursor displays its name and loads .vscode/settings.json.
    //   cursor "<project folder>" --goto "<file>:1"
    std::wstring args = quote(projectRoot);
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
