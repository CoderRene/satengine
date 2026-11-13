#include "EditorState.hpp"

#include "BuildStatusWindow.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorDock.hpp"
#include "NewProjectModal.hpp"
#include "SceneSettingsWindows.hpp"

#include <imgui.h>

namespace sat {

void EditorSession::drawInterface(bool canSaveScene) {
    auto& state = *m_state;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    drawMenuBar(canSaveScene);
    const std::size_t physicsWorlds = state.showPhysicsSettings && state.play.active()
        ? state.play.physicsWorldCount() : 0;
    drawSceneSettingsWindows(state.showLightingSettings, state.showAudioSettings, state.showPhysicsSettings,
        state.scene, state.project, state.play.decisionPending(), state.play.active(), physicsWorlds);
    drawBuildStatusWindow(state.showBuildStatus, state.builder);
    if (drawEditorDiagnostics(state.renderer->assetErrors(), state.play.active(), state.sceneError)) {
        reloadAssets();
    }
    drawEditorDockSpace();

    ImGui::End();

    drawPanels();
    drawNewProjectModal(state.openNewProjectModal, state.newProjectParent, state.newProjectName,
        sizeof(state.newProjectName), &EditorSession::createProjectThunk, this);
}

} // namespace sat
