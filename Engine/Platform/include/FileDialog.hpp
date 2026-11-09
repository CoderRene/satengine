#pragma once

#include <filesystem>

namespace sat {

// Native folder picker. Returns an empty path if the user cancels or the
// platform has no dialog. nativeWindowHandle is an optional GLFWwindow*.
[[nodiscard]] std::filesystem::path openFolderDialog(
    void* nativeWindowHandle,
    const char* title = "Select Folder");

} // namespace sat
