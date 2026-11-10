#pragma once

#include <filesystem>

namespace sat {

// Launches Cursor with SatEngine.code-workspace when present (else projectRoot)
// and optionally --goto file:1. Returns false if Cursor could not be started.
[[nodiscard]] bool openInCursor(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& filePath = {});

} // namespace sat