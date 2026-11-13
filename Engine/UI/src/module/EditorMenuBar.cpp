#include "EditorState.hpp"

#include "FileDialog.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace sat {

void EditorSession::drawMenuBar(bool canSaveScene) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    auto& state = *m_state;
    const bool canSwitchProject = !state.play.decisionPending();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...", nullptr, false, canSwitchProject)) {
            const std::filesystem::path folder =
                openFolderDialog(state.window, "Select Parent Folder");
            if (!folder.empty()) {
                state.assetImports->request([this, folder] {
                    afterStoppingPlay([this, folder] {
                        m_state->newProjectParent = folder;
                        m_state->newProjectName[0] = '\0';
                        m_state->openNewProjectModal = true;
                    });
                });
            }
        }
        if (ImGui::MenuItem("Open Project...", nullptr, false, canSwitchProject)) {
            const std::filesystem::path folder =
                openFolderDialog(state.window, "Open Project Folder");
            if (!folder.empty()) {
                openProject(folder);
            }
        }
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSaveScene)) {
            const bool saved = saveScene();
            (void)saved; // Failure details appear in Scene Errors.
        }
        if (ImGui::MenuItem("Reload Assets", nullptr, false, state.project.isOpen() && !state.play.active())) {
            reloadAssets();
        }
        if (ImGui::BeginMenu("Recent...", canSwitchProject)) {
            const auto& recent = state.recent.entries();
            if (recent.empty()) {
                ImGui::MenuItem("No recent projects", nullptr, false, false);
            } else {
                for (const std::filesystem::path& path : recent) {
                    const std::string full = path.string();
                    ImGui::PushID(full.c_str());
                    const std::string label = path.filename().string();
                    if (ImGui::MenuItem(label.c_str())) {
                        openProject(path);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", full.c_str());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            glfwSetWindowShouldClose(state.window, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene")) {
        ImGui::MenuItem("Audio Settings", nullptr, &state.showAudioSettings);
        ImGui::MenuItem("Lighting Settings", nullptr, &state.showLightingSettings);
        ImGui::MenuItem("Physics Settings", nullptr, &state.showPhysicsSettings);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Build")) {
        const bool building = state.builder.status() == GameBuilder::Status::Building;
        const bool playingNow = state.play.active();
        const bool canBuild = state.project.isOpen() && !playingNow &&
            !state.play.decisionPending() && !building;
        if (ImGui::MenuItem("Build Standalone", nullptr, false, canBuild)) {
            if (saveScene()) {
                const auto obfuscatorFolder = openFolderDialog(state.window,
                    "Optional obfuscator folder (generate.py) - Cancel builds without obfuscation");
                const bool started = state.builder.start(state.project.root(), obfuscatorFolder);
                (void)started; // Failure details are displayed in Build Status.
            }
            state.showBuildStatus = true;
        }
        if (!state.project.isOpen()) {
            ImGui::TextDisabled("Open a project to build.");
        } else if (playingNow) {
            ImGui::TextDisabled("Stop Play mode to build.");
        } else if (building) {
            ImGui::TextDisabled("Build in progress...");
        }
        if (ImGui::MenuItem("Build Status", nullptr, state.showBuildStatus)) {
            state.showBuildStatus = !state.showBuildStatus;
        }
        ImGui::EndMenu();
    }

    if (hasUnsavedChanges()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "* Unsaved changes");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", state.project.isOpen()
                ? (state.play.active() || state.play.decisionPending()
                    ? "Save or discard scene edits after Play."
                    : "Scene edits have not been saved. Use Ctrl+S or File > Save Scene.")
                : "Open or create a project to save your scene.");
        }
    }

    const bool playing = state.play.active();
    ImGui::BeginDisabled(!playing && (!state.project.isOpen() || state.play.decisionPending()));
    ImGui::PushStyleColor(ImGuiCol_Button, playing
        ? ImVec4(0.55f, 0.16f, 0.16f, 1.0f)
        : ImVec4(0.12f, 0.40f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playing
        ? ImVec4(0.70f, 0.22f, 0.22f, 1.0f)
        : ImVec4(0.16f, 0.52f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, playing
        ? ImVec4(0.44f, 0.12f, 0.12f, 1.0f)
        : ImVec4(0.09f, 0.32f, 0.18f, 1.0f));
    // Keep the button within the menu bar's height and stable when toggling.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 0.0f));
    const float buttonWidth = ImGui::CalcTextSize("Stop").x + 24.0f;
    // Center on the host window, keeping clear of the menus at narrow widths.
    const float centeredX = (ImGui::GetWindowWidth() - buttonWidth) * 0.5f;
    if (centeredX > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(centeredX);
    }
    if (ImGui::Button(playing ? "Stop##PlayMode" : "Play##PlayMode", ImVec2(buttonWidth, 0.0f))) {
        if (playing) {
            stopPlay();
        } else {
            startPlay();
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", playing
            ? "Stop the game and review scene edits"
            : (state.project.isOpen()
                ? "Run enabled entity scripts in the Game view"
                : "Open or create a project to play"));
    }
    ImGui::EndDisabled();
    if (state.play.active()) {
        const float fps = ImGui::GetIO().Framerate;
        char fpsText[32] = {};
        std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", fps);
        const float textWidth = ImGui::CalcTextSize(fpsText).x;
        const float rightPad = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - textWidth - rightPad);
        ImGui::TextUnformatted(fpsText);
    }
    ImGui::EndMenuBar();
}

} // namespace sat
