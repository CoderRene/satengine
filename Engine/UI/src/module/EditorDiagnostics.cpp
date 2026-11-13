#include "EditorDiagnostics.hpp"

#include <imgui.h>

namespace sat {

bool drawEditorDiagnostics(std::span<const std::string> assetErrors, bool playMode, std::string_view sceneError) {
    bool reload = false;
    if (!assetErrors.empty()) {
        ImGui::Begin("Asset Errors");
        for (const std::string& error : assetErrors) {
            ImGui::TextWrapped("%s", error.c_str());
        }
        ImGui::BeginDisabled(playMode);
        if (ImGui::Button("Reload Assets")) {
            reload = true;
        }
        ImGui::EndDisabled();
        ImGui::End();
    }
    if (!sceneError.empty()) {
        ImGui::Begin("Scene Errors");
        ImGui::TextWrapped("%.*s", static_cast<int>(sceneError.size()), sceneError.data());
        ImGui::End();
    }
    return reload;
}

} // namespace sat
