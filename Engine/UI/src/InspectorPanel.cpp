#include "InspectorPanel.hpp"

#include "Scene.hpp"
#include "module/PhysicsInspector.hpp"
#include "Project.hpp"
#include "AssetData.hpp"

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
    std::vector<std::string> meshes;
    std::vector<std::string> textures;
    std::string assetError;

    void refreshAssets() {
        meshes.clear();
        textures.clear();
        assetError.clear();
        if (!project->isOpen()) { return; }
        std::error_code error;
        const auto root = project->root() / "Assets";
        std::filesystem::recursive_directory_iterator current(root, error), end;
        while (!error && current != end) {
            if (current->is_regular_file(error)) {
                const auto encoded = current->path().lexically_relative(project->root()).generic_u8string();
                const std::string path(encoded.begin(), encoded.end());
                if (isMeshAssetPath(path)) { meshes.push_back(path); }
                if (isTextureAssetPath(path)) { textures.push_back(path); }
            }
            if (!error) { current.increment(error); }
        }
        if (error) { assetError = error.message(); }
        std::sort(meshes.begin(), meshes.end());
        std::sort(textures.begin(), textures.end());
    }

    bool assetPicker(const char* label, const char* emptyLabel, std::string& path, bool mesh) {
        bool changed = false;
        if (ImGui::BeginCombo(label, path.empty() ? emptyLabel : path.c_str())) {
            if (ImGui::IsWindowAppearing()) { refreshAssets(); }
            if (ImGui::Selectable(emptyLabel, path.empty())) { changed = !path.empty(); path.clear(); }
            const auto& paths = mesh ? meshes : textures;
            for (const auto& candidate : paths) {
                if (ImGui::Selectable(candidate.c_str(), candidate == path)) {
                    changed = candidate != path;
                    path = candidate;
                }
            }
            if (paths.empty()) { ImGui::TextDisabled("Add files to the project's Assets folder."); }
            if (!assetError.empty()) { ImGui::TextWrapped("%s", assetError.c_str()); }
            ImGui::EndCombo();
        }
        return changed;
    }

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

    bool changed = false;
    ImGui::PushID(selectedIndex);
    ImGui::TextUnformatted(entity->name.c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::DragScalarN(
            "Position", ImGuiDataType_Double, &entity->transform.position.x, 3, 0.05f);
        changed |= ImGui::DragFloat3("Rotation", &entity->transform.rotation.x, 0.5f);
        changed |= ImGui::DragFloat3("Scale", &entity->transform.scale.x, 0.05f);
    }

    if (entity->hasCamera &&
        ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::DragFloat("Field of View", &entity->camera.fieldOfView, 0.1f, 1.0f, 179.0f);
        changed |= ImGui::DragFloat("Near Clip", &entity->camera.nearClip, 0.01f, 0.001f, 1000.0f);
        changed |= ImGui::DragFloat("Far Clip", &entity->camera.farClip, 1.0f, 0.1f, 100000.0f);
        changed |= ImGui::ColorEdit4("Clear Color", entity->camera.clearColor);
        bool isMain = entity->camera.isMain;
        if (ImGui::Checkbox("Main Camera", &isMain)) {
            changed = true;
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
        changed = true;
        entity->camera = {};
    }

    if (entity->hasMesh && ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= m_impl->assetPicker("Geometry", "Built-in Cube", entity->mesh.meshPath, true);
        changed |= m_impl->assetPicker("Texture", "None", entity->mesh.texturePath, false);
    }

    if (entity->hasMesh && ImGui::Button("Remove Mesh")) {
        entity->hasMesh = false;
        changed = true;
    }

    int removeScript = -1;
    for (std::size_t index = 0; index < entity->scripts.size(); ++index) {
        auto& script = entity->scripts[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string label = "Lua Script: " + script.path;
        if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::Checkbox("Enabled", &script.enabled);
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
        changed = true;
    }

    changed |= drawPhysicsInspector(*entity);
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) {
        m_impl->refreshScripts();
        ImGui::OpenPopup("AddComponent");
    }
    if (ImGui::BeginPopup("AddComponent")) {
        if (ImGui::MenuItem("Rigid Body", nullptr, false, !entity->hasRigidBody)) {
            entity->hasRigidBody = true; entity->rigidBody = {}; changed = true;
        }
        if (ImGui::MenuItem("Compound Collider", nullptr, false, !entity->hasCollider)) {
            entity->hasCollider = true; entity->collider = {}; changed = true;
        }
        if (ImGui::MenuItem("Gravity Source", nullptr, false, !entity->hasGravitySource)) {
            entity->hasGravitySource = true; entity->gravitySource = {}; changed = true;
        }
        if (ImGui::MenuItem("Mesh (Cube)", nullptr, false, !entity->hasMesh)) {
            entity->mesh = {};
            entity->hasMesh = true;
            changed = true;
        }
        if (ImGui::MenuItem("Camera", nullptr, false, !entity->hasCamera)) {
            entity->camera = {};
            entity->hasCamera = true;
            changed = true;
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
    if (changed) { m_impl->scene->markEdited(); }
    ImGui::PopID();
    ImGui::End();
}

} // namespace sat
