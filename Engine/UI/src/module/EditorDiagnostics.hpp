#pragma once

#include <span>
#include <string>
#include <string_view>

namespace sat {

[[nodiscard]] bool drawEditorDiagnostics(std::span<const std::string> assetErrors, bool playMode,
    std::string_view sceneError);

} // namespace sat
