#include "EditorDock.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>

namespace sat {

void drawEditorDockSpace() {
    const ImGuiID dockspaceId = ImGui::GetID("SatEngineDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f));

    static bool appliedDefaultLayout = false;
    if (!appliedDefaultLayout) {
        appliedDefaultLayout = true;
        const char* ini = ImGui::GetIO().IniFilename;
        const bool hasSavedLayout = ini != nullptr && std::filesystem::exists(ini);
        if (!hasSavedLayout) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockMain = dockspaceId;
            const ImGuiID dockLeft = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
            const ImGuiID dockRight = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
            const ImGuiID dockBottom = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

            ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
            ImGui::DockBuilderDockWindow("Inspector", dockRight);
            ImGui::DockBuilderDockWindow("Scene", dockMain);
            ImGui::DockBuilderDockWindow("Game", dockMain);
            ImGui::DockBuilderDockWindow("Content", dockBottom);
            ImGui::DockBuilderDockWindow("Lua Log", dockBottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
    }
}

} // namespace sat
