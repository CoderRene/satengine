#include "NewProjectModal.hpp"

#include <imgui.h>

namespace sat {

void drawNewProjectModal(bool& open, const std::filesystem::path& parent, char* name, std::size_t nameSize,
    bool (*create)(void* context), void* context) {
    if (open) {
        ImGui::OpenPopup("New Project");
        open = false;
    }

    if (!ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::Text("Parent folder:");
    ImGui::TextWrapped("%s", parent.string().c_str());
    ImGui::Spacing();
    ImGui::InputText("Project name", name, nameSize);

    const bool canCreate = name[0] != '\0';
    if (!canCreate) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)) && create(context)) {
        ImGui::CloseCurrentPopup();
    }
    if (!canCreate) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace sat
