#include "InspectorPanel.hpp"

#include "Scene.hpp"
#include "Project.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sat {

struct InspectorPanel::Impl {
    Scene* scene = nullptr;
    Project* project = nullptr;
    std::vector<std::string> availableScripts;
    std::string scanError;

    void refreshScripts() {
        availableScripts.clear();
        scanError.clear();
        if (!project->isOpen()) {
            return;
        }
        const auto root = project->root() / "Scripts";
        std::error_code error;
        if (!std::filesystem::exists(root, error)) {
            if (error) { scanError = error.message(); }
            return;
        }
        std::filesystem::recursive_directory_iterator current(root, error), end;
        while (!error && current != end) {
            if (current->is_regular_file(error) && current->path().extension() == ".lua") {
                const auto encoded = current->path().lexically_relative(root).generic_u8string();
                const std::string path(reinterpret_cast<const char*>(encoded.data()), encoded.size());
                if (isValidScriptPath(path)) {
                    availableScripts.push_back(path);
                }
            }
            if (!error) { current.increment(error); }
        }
        if (error) { scanError = error.message(); }
        std::sort(availableScripts.begin(), availableScripts.end());
    }
};

InspectorPanel::InspectorPanel(Scene& scene, Project& project)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->scene = &scene;
    m_impl->project = &project;
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::onImGuiRender(int selectedIndex) {
    ImGui::Begin("Inspector");

    Entity* entity = nullptr;
    if (selectedIndex >= 0) {
        entity = m_impl->scene->entity(static_cast<std::size_t>(selectedIndex));
    }

    if (entity == nullptr) {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::End();
        return;
    }

    ImGui::PushID(selectedIndex);
    ImGui::TextUnformatted(entity->name.c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &entity->transform.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &entity->transform.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale", &entity->transform.scale.x, 0.05f);
    }

    if (entity->hasCamera &&
        ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Field of View", &entity->camera.fieldOfView, 0.1f, 1.0f, 179.0f);
        ImGui::DragFloat("Near Clip", &entity->camera.nearClip, 0.01f, 0.001f, 1000.0f);
        ImGui::DragFloat("Far Clip", &entity->camera.farClip, 1.0f, 0.1f, 100000.0f);
        ImGui::ColorEdit4("Clear Color", entity->camera.clearColor);
        bool isMain = entity->camera.isMain;
        if (ImGui::Checkbox("Main Camera", &isMain)) {
            if (isMain) {
                if (!m_impl->scene->setMainCamera(static_cast<std::size_t>(selectedIndex))) {
                    entity->camera.isMain = false;
                }
            } else {
                entity->camera.isMain = false;
            }
        }
    }

    if (entity->hasCamera && ImGui::Button("Remove Camera")) {
        entity->hasCamera = false;
        entity->camera = {};
    }

    if (entity->hasMesh && ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* primitiveName = "Cube";
        switch (entity->mesh.primitive) {
        case MeshComponent::Primitive::Cube:
            primitiveName = "Cube";
            break;
        }
        ImGui::Text("Primitive: %s", primitiveName);
    }

    if (entity->hasMesh && ImGui::Button("Remove Mesh")) {
        entity->hasMesh = false;
    }

    int removeScript = -1;
    for (std::size_t index = 0; index < entity->scripts.size(); ++index) {
        auto& script = entity->scripts[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string label = "Lua Script: " + script.path;
        if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled", &script.enabled);
            ImGui::TextWrapped("Scripts/%s", script.path.c_str());
            std::error_code error;
            if (!m_impl->project->isOpen() ||
                !std::filesystem::is_regular_file(m_impl->project->root() / "Scripts" /
                    std::filesystem::path(std::u8string(script.path.begin(), script.path.end())), error)) {
                ImGui::TextDisabled("Script file is missing.");
            }
            if (ImGui::Button("Remove Script")) {
                removeScript = static_cast<int>(index);
            }
        }
        ImGui::PopID();
    }
    if (removeScript >= 0) {
        entity->scripts.erase(entity->scripts.begin() + removeScript);
    }

    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) {
        m_impl->refreshScripts();
        ImGui::OpenPopup("AddComponent");
    }
    if (ImGui::BeginPopup("AddComponent")) {
        if (ImGui::MenuItem("Mesh (Cube)", nullptr, false, !entity->hasMesh)) {
            entity->mesh = {};
            entity->hasMesh = true;
        }
        if (ImGui::MenuItem("Camera", nullptr, false, !entity->hasCamera)) {
            entity->camera = {};
            entity->hasCamera = true;
        }
        if (ImGui::BeginMenu("Lua Script")) {
            if (!m_impl->project->isOpen()) {
                ImGui::TextDisabled("Open a project first.");
            } else if (m_impl->availableScripts.empty()) {
                ImGui::TextDisabled("Add .lua files to the project's Scripts folder.");
            }
            if (!m_impl->scanError.empty()) {
                ImGui::TextWrapped("Cannot list scripts: %s", m_impl->scanError.c_str());
            }
            for (const auto& path : m_impl->availableScripts) {
                const bool attached = std::any_of(entity->scripts.begin(), entity->scripts.end(),
                    [&](const ScriptComponent& component) { return component.path == path; });
                if (ImGui::MenuItem(path.c_str(), nullptr, attached, !attached)) {
                    const bool added = m_impl->scene->attachScript(static_cast<std::size_t>(selectedIndex), path);
                    (void)added; // Valid path and duplicate checks were applied above.
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::End();
}

} // namespace sat
