#include "EditorState.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <future>

namespace sat {

bool EditorSession::requestClose() {
    auto& state = *m_state;
    if (state.closeApproved) { return true; }
    if (sceneBusy() || state.play.decisionPending()) { return false; }
    if (state.play.active()) {
        stopPlay();
        if (state.play.decisionPending()) {
            state.play.defer([this] {
                glfwSetWindowShouldClose(m_state->window, GLFW_TRUE);
            });
            return false;
        }
    }
    if (state.viewport && state.viewport->transformActive()) { resetViewportInteraction(); }
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
    if (sceneBusy()) {
        drawSceneProgress();
        return;
    }
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
    if (sceneBusy()) { drawSceneProgress(); }
}

void EditorSession::drawSceneProgress() {
    auto& state = *m_state;
    const bool startingPlay = state.playPreparation.valid();
    const char* popup = startingPlay ? "Starting Play" : "Opening Project";
    if (!ImGui::IsPopupOpen(popup)) {
        ImGui::OpenPopup(popup);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* step = startingPlay ? "Preparing scene..." : !state.preparingProject ? "Loading scene..." :
            (state.project.isOpen() ? "Saving scene and preparing project..." : "Preparing project...");
        ImGui::TextUnformatted(step);
        ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), ImVec2(360.0f, 0.0f), "");
        // Present a frame of progress before consuming even an immediately ready job.
        if (state.sceneProgressShown) {
            constexpr auto kPoll = std::chrono::seconds(0);
            if (startingPlay && state.playPreparation.wait_for(kPoll) == std::future_status::ready) {
                finishPlayPreparation();
            } else if (!startingPlay && state.projectOpen.wait_for(kPoll) == std::future_status::ready) {
                finishProjectOpen();
            }
            if (!sceneBusy()) { ImGui::CloseCurrentPopup(); }
        } else {
            state.sceneProgressShown = true;
        }
        ImGui::EndPopup();
    }
}

} // namespace sat
