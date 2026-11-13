#include "BuildStatusWindow.hpp"

#include "GameBuilder.hpp"

#include <imgui.h>

#include <cstdio>

namespace sat {

void drawBuildStatusWindow(bool& open, const GameBuilder& builder) {
    if (!open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(680.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build Status", &open)) {
        if (builder.status() == GameBuilder::Status::Idle) {
            ImGui::TextUnformatted("Choose Build > Build Standalone to export the open project.");
        } else {
            ImGui::TextWrapped("%s", builder.message().c_str());
            char progressText[64] = {};
            std::snprintf(progressText, sizeof(progressText), "%.0f%% (build stages)", builder.progress() * 100.0f);
            ImGui::ProgressBar(builder.progress(), ImVec2(-1.0f, 0.0f), progressText);
            ImGui::Text("Elapsed: %.0f seconds", builder.elapsedSeconds());
            if (builder.status() == GameBuilder::Status::Building) {
                ImGui::TextDisabled("Stage progress; compilation time varies. Live output appears below.");
                if (builder.secondsSinceOutput() >= 10.0) {
                    ImGui::TextDisabled("Last output %.0f seconds ago", builder.secondsSinceOutput());
                }
            }
            if (!builder.outputFolder().empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("Output: %s", builder.outputFolder().string().c_str());
                ImGui::TextWrapped("Executable: %s", builder.executablePath().string().c_str());
                ImGui::TextWrapped("Log: %s", (builder.outputFolder() / "build.log").string().c_str());
            }
            ImGui::Separator();
            if (ImGui::BeginChild("BuildOutput", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
                const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                if (builder.logOutput().empty()) {
                    ImGui::TextUnformatted("Waiting for build output...");
                } else {
                    ImGui::TextUnformatted(builder.logOutput().c_str());
                }
                if (atBottom) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

} // namespace sat
