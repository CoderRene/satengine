#include "EditorState.hpp"

#include "BuildStatusWindow.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorDock.hpp"
#include "NewProjectModal.hpp"
#include "Renderer.hpp"
#include "SceneSettingsWindows.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <span>

namespace sat {

void EditorSession::drawInterface(bool canSaveScene) {
    auto& state = *m_state;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    constexpr float kHeaderInset = 8.0f;
    constexpr float kHeaderPaddingY = 8.0f;
    constexpr ImU32 kHeaderBorder = IM_COL32(132, 148, 169, 255);
    const ImVec4 canvas = ImGui::GetStyleColorVec4(ImGuiCol_DockingEmptyBg);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, canvas);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));
    ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, kHeaderPaddingY);
    ImGui::Begin("DockSpace", nullptr, hostFlags);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    const ImGuiWindow* host = ImGui::GetCurrentWindowRead();
    const ImRect menuBar = host->MenuBarRect();
    const ImVec2 headerMin(menuBar.Min.x + kHeaderInset, menuBar.Min.y + 2.0f);
    const ImVec2 headerMax(menuBar.Max.x - kHeaderInset, menuBar.Max.y - 1.0f);
    if (headerMax.x > headerMin.x && headerMax.y > headerMin.y) {
        // The content clip starts below the menu bar. Draw its surface in the host clip.
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(host->OuterRectClipped.Min, host->OuterRectClipped.Max, false);
        const float rounding = ImGui::GetStyle().WindowRounding;
        drawList->AddRectFilled(headerMin, headerMax, ImGui::GetColorU32(ImGuiCol_MenuBarBg), rounding);
        drawList->AddRect(headerMin, headerMax, ImGui::GetColorU32(kHeaderBorder), rounding);
        drawList->PopClipRect();
    }

    if (!sceneBusy()) {
        ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, kHeaderPaddingY);
        drawMenuBar(canSaveScene);
        ImGui::PopStyleVar();
    }
    const bool busy = sceneBusy();
    if (!busy) {
        const std::size_t physicsWorlds = state.showPhysicsSettings && state.play.active()
            ? state.play.physicsWorldCount() : 0;
        const auto settingsRevision = state.scene.editRevision();
        drawSceneSettingsWindows(state.showLightingSettings, state.showAudioSettings, state.showPhysicsSettings,
            state.scene, state.project, state.play.decisionPending() || (state.viewport && state.viewport->transformActive()),
            state.play.active(), physicsWorlds);
        if (state.recordsSceneHistory() && state.scene.editRevision() != settingsRevision) {
            const auto active = ImGui::GetActiveID();
            const auto gesture = active != 0 ? active : ImGui::GetCurrentContext()->DeactivatedItemData.ID;
            state.history.capture(state.scene, "Scene Settings", gesture, std::span<const EntityId>{});
        }
        drawBuildStatusWindow(state.showBuildStatus, state.builder);
        if (drawEditorDiagnostics(state.renderer->assetErrors(), state.play.active(), state.sceneError)) {
            reloadAssets();
        }
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, canvas);
    drawEditorDockSpace();
    ImGui::PopStyleColor();

    ImGui::End();

    if (busy) { return; }
    drawPanels();
    drawNewProjectModal(state.openNewProjectModal, state.newProjectParent, state.newProjectName,
        sizeof(state.newProjectName), &EditorSession::createProjectThunk, this);
}

} // namespace sat
