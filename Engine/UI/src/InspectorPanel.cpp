#include "InspectorPanel.hpp"

#include "Scene.hpp"
#include "module/PhysicsInspector.hpp"
#include "module/CursorWrap.hpp"
#include "Project.hpp"
#include "AssetData.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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
    std::vector<std::string> materials;
    std::vector<std::string> clips;
    std::string assetError;

    void refreshAssets() {
        meshes.clear();
        materials.clear();
        clips.clear();
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
                if (isMaterialAssetPath(path)) { materials.push_back(path); }
                if (isAudioAssetPath(path)) { clips.push_back(path); }
            }
            if (!error) { current.increment(error); }
        }
        if (error) { assetError = error.message(); }
        std::sort(meshes.begin(), meshes.end());
        std::sort(materials.begin(), materials.end());
        std::sort(clips.begin(), clips.end());
    }

    bool assetPicker(const char* label, const char* emptyLabel, std::string& path, const std::vector<std::string>& paths) {
        bool changed = false;
        if (ImGui::BeginCombo(label, path.empty() ? emptyLabel : path.c_str())) {
            if (ImGui::IsWindowAppearing()) { refreshAssets(); }
            if (ImGui::Selectable(emptyLabel, path.empty())) { changed = !path.empty(); path.clear(); }
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

void InspectorPanel::onImGuiRender(int selectedIndex, Entity* liveEntity) {
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
        Transform& visible = liveEntity ? liveEntity->transform : entity->transform;
        const auto editAxis = [&](const char* label, ImGuiDataType type, auto& authored, auto& live, float speed) {
            if (!dragScalar(label, type, &live, speed)) { return; }
            authored = live;
            changed = true;
        };
        const auto editVector = [&](const char* label, ImGuiDataType type, auto& authored, auto& live, float speed) {
            ImGui::BeginGroup();
            ImGui::PushID(label);
            ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
            editAxis("##X", type, authored.x, live.x, speed);
            ImGui::PopItemWidth();
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            editAxis("##Y", type, authored.y, live.y, speed);
            ImGui::PopItemWidth();
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            editAxis("##Z", type, authored.z, live.z, speed);
            ImGui::PopItemWidth();
            ImGui::PopID();
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(label);
            ImGui::EndGroup();
        };
        editVector("Position", ImGuiDataType_Double, entity->transform.position, visible.position, 0.05f);
        editVector("Rotation", ImGuiDataType_Float, entity->transform.rotation, visible.rotation, 0.5f);
        editVector("Scale", ImGuiDataType_Float, entity->transform.scale, visible.scale, 0.05f);
    }

    if (entity->hasCamera &&
        ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= dragFloat("Field of View", &entity->camera.fieldOfView, 0.1f, 1.0f, 179.0f);
        changed |= dragFloat("Near Clip", &entity->camera.nearClip, 0.01f, 0.001f, 1000.0f);
        changed |= dragFloat("Far Clip", &entity->camera.farClip, 1.0f, 0.1f, 100000.0f);
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
        changed |= m_impl->assetPicker("Geometry", "Built-in Cube", entity->mesh.meshPath, m_impl->meshes);
        changed |= m_impl->assetPicker("Material", "Default", entity->mesh.materialPath, m_impl->materials);
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
    if (entity->hasAudioSource && ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& source = entity->audioSource;
        changed |= m_impl->assetPicker("Clip", "None", source.clipPath, m_impl->clips);
        changed |= dragFloat("Volume", &source.volume, 0.01f, 0.0f, 16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= dragFloat("Pitch", &source.pitch, 0.01f, 0.01f, 16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::Checkbox("Loop", &source.loop);
        changed |= ImGui::Checkbox("Play on Start", &source.playOnStart);
        changed |= ImGui::Checkbox("Spatial", &source.spatial);
        if (source.spatial) {
            changed |= dragFloat("Minimum Distance (m)", &source.minDistance, 0.1f,
                0.01f, std::max(0.01f, source.maxDistance), "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= dragFloat("Maximum Distance (m)", &source.maxDistance, 0.1f,
                source.minDistance, std::max(100000.0f, source.minDistance), "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= dragFloat("Rolloff", &source.rolloff, 0.01f, 0.0f, 16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed |= dragFloat("Doppler Factor", &source.dopplerFactor, 0.01f, 0.0f, 16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        int bus = static_cast<int>(source.bus);
        if (ImGui::Combo("Bus", &bus, "SFX\0Music\0UI\0")) {
            source.bus = static_cast<AudioBus>(bus);
            changed = true;
        }
        changed |= ImGui::InputInt("Priority", &source.priority);
        if (ImGui::Button("Remove Audio Source")) {
            entity->hasAudioSource = false;
            entity->audioSource = {};
            changed = true;
        }
    }
    if (entity->hasLight && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& light = entity->light;
        int type = static_cast<int>(light.type);
        if (ImGui::Combo("Light Type", &type, "Directional\0Point\0Spot\0")) {
            light.type = static_cast<LightType>(type);
            changed = true;
        }
        changed |= ImGui::ColorEdit3("Light Color", light.color);
        changed |= dragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 10000.0f,
            "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (light.type != LightType::Directional) {
            changed |= dragFloat("Range", &light.range, 0.1f, 0.01f, 100000.0f,
                "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (light.type == LightType::Spot) {
            changed |= ImGui::SliderFloat("Inner Cone", &light.innerConeDegrees, 0.0f,
                light.outerConeDegrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Outer Cone", &light.outerConeDegrees, light.innerConeDegrees,
                89.9f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        }
        if (ImGui::Button("Remove Light")) { entity->hasLight = false; changed = true; }
    }
    if (entity->hasAudioListener && ImGui::CollapsingHeader("Audio Listener", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Remove Audio Listener")) {
            entity->hasAudioListener = false;
            changed = true;
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) {
        m_impl->refreshScripts();
        ImGui::OpenPopup("AddComponent");
    }
    if (ImGui::BeginPopup("AddComponent")) {
        if (ImGui::MenuItem("Light", nullptr, false, !entity->hasLight)) {
            entity->hasLight = true;
            entity->light = {};
            changed = true;
        }
        if (ImGui::MenuItem("Audio Source", nullptr, false, !entity->hasAudioSource)) {
            entity->hasAudioSource = true;
            entity->audioSource = {};
            changed = true;
        }
        if (ImGui::MenuItem("Audio Listener", nullptr, false, !entity->hasAudioListener)) {
            entity->hasAudioListener = true;
            changed = true;
        }
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
