#pragma once

#include <imgui.h>

#include <cstddef>
#include <string_view>

namespace sat {

inline constexpr char kContentFilePayload[] = "SAT_CONTENT_FILE";

// The grabbed file comes first, followed by the rest of the selection.
[[nodiscard]] inline std::string_view draggedContentPath(std::string_view paths) {
    const auto end = paths.find('\0');
    return end == std::string_view::npos ? std::string_view{} : paths.substr(0, end);
}

[[nodiscard]] inline std::string_view draggedContentPath(const ImGuiPayload* payload) {
    if (!payload || !payload->IsDataType(kContentFilePayload) || payload->DataSize <= 0) {
        return {};
    }
    return draggedContentPath({static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize)});
}

} // namespace sat
