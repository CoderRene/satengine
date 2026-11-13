#include "EditorState.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <utility>

namespace sat {

bool EditorSession::requestClose() {
    auto& state = *m_state;
    if (state.closeApproved) { return true; }
    if (state.play.decisionPending()) { return false; }
    if (state.play.active()) {
        stopPlay();
        if (state.play.decisionPending()) {
            state.play.defer([this] {
                glfwSetWindowShouldClose(m_state->window, GLFW_TRUE);
            });
            return false;
        }
    }
    if (!hasUnsavedChanges() && !state.assetImports->dirty()) {
        return true;
    }
    state.assetImports->request([this] {
        if (hasUnsavedChanges()) {
            m_state->openClosePrompt = true;
        } else {
            m_state->closeApproved = true;
            glfwSetWindowShouldClose(m_state->window, GLFW_TRUE);
        }
    });
    return false;
}

void EditorSession::drawPrompts() {
    auto& state = *m_state;
    state.assetImports->drawPrompt();

    if (state.play.decisionPending() && !ImGui::IsPopupOpen("Play Mode Changes")) {
        ImGui::OpenPopup("Play Mode Changes");
    }
    if (ImGui::BeginPopupModal("Play Mode Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save the scene edits you made during Play?");
        if (!state.sceneError.empty()) {
            ImGui::TextWrapped("%s", state.sceneError.c_str());
        }
        if (ImGui::Button("Save Changes") && saveScene()) {
            state.play.acceptEdits();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard Changes")) {
            discardPlayEdits();
        }
        const bool resolved = !state.play.decisionPending();
        if (resolved) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
        if (resolved) {
            auto action = state.play.takePendingAction();
            if (action) { action(); }
        }
    }

    if (state.openClosePrompt) {
        ImGui::OpenPopup("Unsaved Scene Changes");
        state.openClosePrompt = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save scene changes before exiting the editor?");
        if (!state.sceneError.empty()) {
            ImGui::TextWrapped("%s", state.sceneError.c_str());
        }
        if (!state.project.isOpen()) {
            ImGui::TextUnformatted("Open or create a project to save this scene.");
        }
        ImGui::BeginDisabled(!state.project.isOpen());
        if (ImGui::Button("Save and Exit")) {
            if (saveScene()) {
                state.closeApproved = true;
                glfwSetWindowShouldClose(state.window, GLFW_TRUE);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Exit Without Saving")) {
            state.discardSceneOnClose = true;
            state.closeApproved = true;
            glfwSetWindowShouldClose(state.window, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace sat
