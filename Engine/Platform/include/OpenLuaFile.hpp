#pragma once

#include <filesystem>

namespace sat {

// Opens projectRoot in Cursor and optionally jumps to line 1 of filePath.
// Returns false if Cursor could not be started.
[[nodiscard]] bool openInCursor(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& filePath = {});

} // namespace sat