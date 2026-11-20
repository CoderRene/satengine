#include "EditorState.hpp"

#include "FileDialog.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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

    const auto& io = ImGui::GetIO();
    const bool historyEnabled = state.recordsSceneHistory() && !state.openClosePrompt &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        (!state.viewport || !state.viewport->transformActive()) && !io.WantTextInput && !ImGui::IsAnyItemActive();
    if (historyEnabled) { state.history.finishGesture(); }
    bool undo = historyEnabled && io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false);
    bool redo = historyEnabled && io.KeyCtrl && !io.KeyAlt && !io.KeySuper &&
        (io.KeyShift ? ImGui::IsKeyPressed(ImGuiKey_Z, false) : ImGui::IsKeyPressed(ImGuiKey_Y, false));
    if (ImGui::BeginMenu("Edit")) {
        const char* undoLabel = state.history.undoLabel();
        const char* redoLabel = state.history.redoLabel();
        const auto undoText = undoLabel ? std::string("Undo ") + undoLabel : std::string("Undo");
        const auto redoText = redoLabel ? std::string("Redo ") + redoLabel : std::string("Redo");
        const bool canStep = state.recordsSceneHistory() && (!state.viewport || !state.viewport->transformActive());
        undo |= ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, canStep && undoLabel);
        redo |= ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, canStep && redoLabel);
        ImGui::EndMenu();
    }
    if ((undo || redo) && !state.history.needsCapture(state.scene)) {
        const auto selection = state.history.step(state.scene, undo);
        if (state.hierarchy && selection) { state.hierarchy->restoreSelection(selection->ids, selection->primary); }
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
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_UnsavedMarker), "* Unsaved changes");
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
        ? ImVec4(0.71f, 0.26f, 0.21f, 1.0f)
        : ImVec4(0.16f, 0.39f, 0.73f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playing
        ? ImVec4(0.75f, 0.28f, 0.23f, 1.0f)
        : ImVec4(0.19f, 0.42f, 0.76f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, playing
        ? ImVec4(0.61f, 0.22f, 0.19f, 1.0f)
        : ImVec4(0.15f, 0.34f, 0.63f, 1.0f));
    // Keep the button within the menu bar's height and stable when toggling.
    constexpr float kButtonPaddingX = 14.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kButtonPaddingX, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    const float buttonWidth = ImGui::CalcTextSize("Stop").x + 2.0f * kButtonPaddingX;
    // Center on the host window, keeping clear of the menus at narrow widths.
    const float centeredX = (ImGui::GetWindowWidth() - buttonWidth) * 0.5f;
    if (centeredX > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(centeredX);
    }
    if (ImGui::ButtonEx(playing ? "Stop##PlayMode" : "Play##PlayMode", ImVec2(buttonWidth, 0.0f),
            ImGuiButtonFlags_AlignTextBaseLine)) {
        if (playing) {
            stopPlay();
        } else {
            startPlay();
        }
    }
    ImGui::PopStyleVar(2);
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
