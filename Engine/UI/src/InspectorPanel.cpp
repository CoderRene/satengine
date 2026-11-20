#include "InspectorPanel.hpp"
#include "AnimationAsset.hpp"
#include "AnimationGraph.hpp"
#include "AnimationSystem.hpp"
#include "AssetLibrary.hpp"
#include "module/AnimationGraphPanel.hpp"
#include "module/AssetWrite.hpp"
#include "module/ContentDragDrop.hpp"

#include "ContentPanel.hpp"
#include "Scene.hpp"
#include "LuaScriptFields.hpp"
#include "module/PhysicsInspector.hpp"
#include "module/HierarchyDragDrop.hpp"
#include "module/CursorWrap.hpp"
#include "Project.hpp"
#include "AssetData.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sat {
namespace {

constexpr auto kScriptRefreshInterval = std::chrono::milliseconds(200);

bool editEntityReference(const char* label, ScriptEntityReference& reference, const Scene& scene) {
    const Entity* target = scene.entityById(reference.entityId);
    const char* display = target ? target->name.c_str() :
        (reference.entityId == kInvalidEntityId ? "Drop an entity here" : "Missing entity");
    ImGui::Button("##entityReference", ImVec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight()));
    const ImVec2 position = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    auto* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(position, ImVec2(end.x - padding.x, end.y), true);
    drawList->AddText(ImVec2(position.x + padding.x, position.y + padding.y),
        ImGui::GetColorU32(ImGuiCol_Text), display);
    drawList->PopClipRect();
    bool changed = false;
    if (!(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) && ImGui::BeginDragDropTarget()) {
        const EntityId dragged = hierarchyPayloadEntity(ImGui::GetDragDropPayload());
        if (scene.entityById(dragged) && ImGui::AcceptDragDropPayload(kEntityPayload) && dragged != reference.entityId) {
            reference.entityId = dragged;
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    return changed;
}

bool editScriptField(const char* label, ScriptFieldValue& value, const Scene& scene) {
    return std::visit([label, &scene](auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
            return ImGui::Checkbox(label, &item);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            ImS64 integer = item;
            if (!ImGui::InputScalar(label, ImGuiDataType_S64, &integer)) {
                return false;
            }
            item = integer;
            return true;
        } else if constexpr (std::is_same_v<T, double>) {
            return ImGui::InputDouble(label, &item, 0.0, 0.0, "%.17g") && std::isfinite(item);
        } else if constexpr (std::is_same_v<T, ScriptEntityReference>) {
            return editEntityReference(label, item, scene);
        } else {
            std::array<char, kMaxScriptFieldTextBytes + 1> text;
            item.copy(text.data(), item.size());
            text[item.size()] = '\0';
            if (!ImGui::InputText(label, text.data(), text.size())) {
                return false;
            }
            const std::string_view edited(text.data());
            if (!isValidScriptFieldText(edited)) {
                return false;
            }
            item = edited;
            return true;
        }
    }, value);
}

} // namespace

struct InspectorPanel::Impl {
    Scene* scene = nullptr;
    Project* project = nullptr;
    std::vector<std::string> availableScripts;
    std::array<char, 256> m_scriptSearch{};
    std::string scanError;

    struct FieldCache {
        std::chrono::steady_clock::time_point m_nextRefresh;
        LuaScriptFieldPreview m_preview;
    };
    std::filesystem::path m_fieldProjectRoot;
    EntityId m_fieldEntity = kInvalidEntityId;
    std::unordered_map<std::string, FieldCache> m_scriptFields;
    std::vector<std::string> meshes;
    std::vector<std::string> materials;
    std::vector<std::string> animationClips, controllers;
    bool controllerMode = false;
    std::vector<std::string> clips;
    std::vector<std::string> uiDocuments;
    std::vector<std::string> uiFonts;
    std::string assetError;

    void refreshAssets() {
        meshes.clear();
        materials.clear();
        animationClips.clear(); controllers.clear();
        clips.clear();
        uiDocuments.clear();
        uiFonts.clear();
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
                if (isAnimationClipAssetPath(path)) { animationClips.push_back(path); }
                if (isAnimationGraphAssetPath(path)) { controllers.push_back(path); }
                if (isMaterialAssetPath(path)) { materials.push_back(path); }
                if (isAudioAssetPath(path)) { clips.push_back(path); }
                if (isUiDocumentPath(path)) { uiDocuments.push_back(path); }
                if (isUiFontPath(path)) { uiFonts.push_back(path); }
            }
            if (!error) { current.increment(error); }
        }
        if (error) { assetError = error.message(); }
        std::sort(meshes.begin(), meshes.end());
        std::sort(materials.begin(), materials.end());
        std::sort(clips.begin(), clips.end());
        std::sort(uiDocuments.begin(), uiDocuments.end());
        std::sort(uiFonts.begin(), uiFonts.end());
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

    bool drawScriptFields(ScriptComponent& script) {
        auto [entry, inserted] = m_scriptFields.try_emplace(script.path);
        auto& cache = entry->second;
        const auto now = std::chrono::steady_clock::now();
        if (inserted || now >= cache.m_nextRefresh) {
            const auto path = m_fieldProjectRoot / "Scripts" /
                std::filesystem::path(std::u8string(script.path.begin(), script.path.end()));
            cache.m_preview.refresh(path);
            cache.m_nextRefresh = now + kScriptRefreshInterval;
        }
        const auto& defaults = cache.m_preview.defaults();
        const auto& error = cache.m_preview.error();
        if (!error.empty()) {
            ImGui::TextWrapped("%s", error.c_str());
            if (defaults.empty()) {
                return false;
            }
            ImGui::TextDisabled("Showing the last valid script fields.");
        }

        bool changed = false;
        auto& overrides = script.fieldOverrides;
        ImGui::BeginDisabled(!error.empty());
        for (const auto& name : cache.m_preview.fieldOrder()) {
            const auto& defaultValue = defaults.at(name);
            const auto saved = overrides.find(name);
            auto value = saved != overrides.end() && saved->second.index() == defaultValue.index()
                ? saved->second : defaultValue;
            ImGui::PushID(name.c_str());
            ImGui::BeginDisabled(saved == overrides.end() && overrides.size() >= kMaxScriptFields);
            if (editScriptField(name.c_str(), value, *scene)) {
                overrides.insert_or_assign(name, std::move(value));
                changed = true;
            }
            ImGui::EndDisabled();
            if (saved != overrides.end()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##scriptField")) {
                    overrides.erase(saved);
                    changed = true;
                }
            }
            ImGui::PopID();
        }
        for (auto saved = overrides.begin(); saved != overrides.end();) {
            if (defaults.contains(saved->first)) {
                ++saved;
                continue;
            }
            ImGui::PushID(saved->first.c_str());
            ImGui::TextDisabled("%s (field removed)", saved->first.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##scriptField")) {
                saved = overrides.erase(saved);
                changed = true;
            } else {
                ++saved;
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
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

void InspectorPanel::onImGuiRender(int selectedIndex, Entity* liveEntity, ContentPanel* content, AnimationSystem* animation, AnimationGraphPanel* graph) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    Entity* entity = nullptr;
    if (selectedIndex >= 0) {
        entity = m_impl->scene->entity(static_cast<std::size_t>(selectedIndex));
    }

    const auto fieldEntity = entity ? entity->id() : kInvalidEntityId;
    if (m_impl->m_fieldEntity != fieldEntity || m_impl->m_fieldProjectRoot != m_impl->project->root()) {
        m_impl->m_scriptFields.clear();
        m_impl->m_fieldEntity = fieldEntity;
        m_impl->controllerMode = entity && !entity->animator.controllerPath.empty();
        m_impl->m_fieldProjectRoot = m_impl->project->root();
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
        Transform authoredTransform = entity->transform;
        Transform visible = liveEntity ? liveEntity->transform : authoredTransform;
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
        editVector("Position", ImGuiDataType_Double, authoredTransform.position, visible.position, 0.05f);
        editVector("Rotation", ImGuiDataType_Float, authoredTransform.rotation, visible.rotation, 0.5f);
        editVector("Scale", ImGuiDataType_Float, authoredTransform.scale, visible.scale, 0.05f);
        if (changed) {
            m_impl->scene->setTransform(entity->id(), authoredTransform);
            if (liveEntity) { liveEntity->transform = visible; }
        }
    }

    const auto componentHeader = [content, &changed](const char* label, auto&& remove,
        std::initializer_list<std::string_view> paths = {}) {
        bool keep = true;
        const bool expanded = ImGui::CollapsingHeader(label, &keep, ImGuiTreeNodeFlags_DefaultOpen);
        if (!keep) {
            remove();
            changed = true;
            return false;
        }
        if (content && paths.size() != 0 && ImGui::IsItemClicked()) {
            content->revealReferences(paths);
        }
        return expanded;
    };

    if (entity->hasCamera &&
        componentHeader("Camera", [&] {
            entity->hasCamera = false;
            entity->camera = {};
        })) {
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

    if (entity->hasMesh && componentHeader("Mesh", [&] {
        entity->hasMesh = false;
        entity->mesh = {};
    }, {entity->mesh.meshPath, entity->mesh.materialPath})) {
        if (m_impl->assetPicker("Geometry", "Built-in Cube", entity->mesh.meshPath, m_impl->meshes)) {
            entity->mesh.meshNode = 0;
            changed = true;
        }
        const char* defaultMaterial = isSceneModelAssetPath(entity->mesh.meshPath) ? "Imported Materials" : "Default";
        changed |= m_impl->assetPicker("Material", defaultMaterial, entity->mesh.materialPath, m_impl->materials);
        if (!entity->mesh.meshPath.empty() && entity->mesh.meshNode == 0) {
            changed |= ImGui::Checkbox("Combine Static at Runtime", &entity->mesh.combineStatic);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Combine nearby imported geometry that uses the same material.");
            }
        }
    }

    int removeScript = -1;
    for (std::size_t index = 0; index < entity->scripts.size(); ++index) {
        auto& script = entity->scripts[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string label = "Lua Script: " + script.path;
        const auto scriptPath = isValidScriptPath(script.path) ? "Scripts/" + script.path : std::string{};
        if (content && content->luaIcon() != 0) {
            const float iconSize = ImGui::GetFrameHeight();
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(content->luaIcon())), ImVec2(iconSize, iconSize));
            if (ImGui::IsItemClicked()) {
                content->revealReferences({scriptPath});
            }
            ImGui::SameLine();
        }
        if (componentHeader(label.c_str(), [&] {
            removeScript = static_cast<int>(index);
        }, {scriptPath})) {
            changed |= ImGui::Checkbox("Enabled", &script.enabled);
            ImGui::TextWrapped("Scripts/%s", script.path.c_str());
            ImGui::Separator();
            if (m_impl->project->isOpen() && isValidScriptPath(script.path)) {
                changed |= m_impl->drawScriptFields(script);
            }
        }
        ImGui::PopID();
    }
    if (removeScript >= 0) {
        m_impl->m_scriptFields.erase(entity->scripts[static_cast<std::size_t>(removeScript)].path);
        entity->scripts.erase(entity->scripts.begin() + removeScript);
        changed = true;
    }

    if (entity->gameUi && componentHeader("Game UI", [&] {
        entity->gameUi.reset();
    }, {entity->gameUi->documentPath, entity->gameUi->fontPath})) {
        auto& ui = *entity->gameUi;
        changed |= m_impl->assetPicker("Document", "None", ui.documentPath, m_impl->uiDocuments);
        changed |= m_impl->assetPicker("Font", "None", ui.fontPath, m_impl->uiFonts);
        changed |= ImGui::Checkbox("Show on Start", &ui.visible);
    }

    changed |= drawPhysicsInspector(*entity);
    if (entity->hasAnimator && componentHeader("Animator", [&] {
        entity->hasAnimator = false;
        entity->animator = {};
        m_impl->controllerMode = false;
    })) {
        auto& animator = entity->animator;
        int mode = m_impl->controllerMode ? 1 : 0;
        if (ImGui::Combo("Mode", &mode, "Clip\0Controller\0")) {
            m_impl->controllerMode = mode == 1;
            if (!m_impl->controllerMode) { animator.controllerPath.clear(); changed = true; }
        }
        if (m_impl->controllerMode) {
            changed |= m_impl->assetPicker("Controller", "None", animator.controllerPath, m_impl->controllers);
            if (ImGui::BeginDragDropTarget()) {
                const auto path = draggedContentPath(ImGui::GetDragDropPayload());
                if (isAnimationGraphAssetPath(path) && ImGui::AcceptDragDropPayload(kContentFilePayload)) { animator.controllerPath = path; changed = true; }
                ImGui::EndDragDropTarget();
            }
            if (graph && !animator.controllerPath.empty() && ImGui::Button("Edit Controller")) { graph->open(animator.controllerPath); }
            if (ImGui::Button("New Controller")) {
                AnimationGraph controller;
                std::string clip = animator.clip;
                if (clip.empty() && animation) { const auto names = animation->clipNames(animatorMeshPath(*m_impl->scene, *entity)); if (!names.empty()) { clip = names.front(); } }
                controller.states.push_back({"Idle", clip});
                std::string bytes;
                std::string path = "Assets/Controller.satanimfsm";
                std::error_code error;
                unsigned suffix = 1;
                while (std::filesystem::exists(m_impl->project->root() / path, error) && !error) { path = "Assets/Controller" + std::to_string(suffix++) + ".satanimfsm"; }
                if (!error && encodeAnimationGraph(controller, bytes, m_impl->assetError) &&
                    writeAsset(m_impl->project->root() / path, path, bytes, m_impl->assetError)) {
                    animator.controllerPath = path; changed = true; if (graph) { graph->open(path); }
                }
            }
        } else {
            changed |= m_impl->assetPicker("Clip Asset", "Embedded clip", animator.clip, m_impl->animationClips);
            if (ImGui::BeginDragDropTarget()) {
                const auto path = draggedContentPath(ImGui::GetDragDropPayload());
                if (isAnimationClipAssetPath(path) && ImGui::AcceptDragDropPayload(kContentFilePayload)) { animator.clip = path; changed = true; }
                ImGui::EndDragDropTarget();
            }
            if (animation && ImGui::BeginCombo("Embedded Clip", animator.clip.c_str())) {
                for (const auto& name : animation->clipNames(animatorMeshPath(*m_impl->scene, *entity))) {
                    if (ImGui::Selectable(name.c_str(), name == animator.clip)) { animator.clip = name; changed = true; }
                }
                ImGui::EndCombo();
            }
        }
        if (!m_impl->controllerMode) {
            changed |= ImGui::Checkbox("Play On Start", &animator.playOnStart);
            changed |= ImGui::Checkbox("Loop Animation", &animator.loop);
        }
        changed |= ImGui::InputFloat("Playback Speed", &animator.speed);
        auto rotationBlend = static_cast<int>(animator.rotationBlend);
        if (ImGui::Combo("Rotation Interpolation", &rotationBlend, "Slerp\0Nlerp\0")) {
            animator.rotationBlend = static_cast<AnimationRotationBlend>(rotationBlend);
            changed = true;
        }
        auto cullingMode = static_cast<int>(animator.cullingMode);
        if (ImGui::Combo("Culling Mode", &cullingMode, "Always Animate\0Automatic\0")) {
            animator.cullingMode = static_cast<AnimationCullingMode>(cullingMode);
            changed = true;
        }
        if (animator.cullingMode == AnimationCullingMode::Automatic) {
            float radius = animator.occlusionRadius;
            if (ImGui::InputFloat("Occlusion Radius", &radius) && std::isfinite(radius) && radius >= 0) {
                animator.occlusionRadius = radius;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Standalone decorative meshes only. Enclose every pose in a local sphere "
                    "centered on the entity pivot. Set to 0 to disable. Directional shadows must be off.");
            }
        }
        if (!m_impl->controllerMode) {
            changed |= ImGui::DragFloat("Crossfade Seconds", &animator.crossfadeSeconds, 0.01f, 0, 60);
        } else if (const float defaultCrossfade = AnimatorComponent{}.crossfadeSeconds;
            animator.crossfadeSeconds != defaultCrossfade) {
            animator.crossfadeSeconds = defaultCrossfade;
            changed = true;
        }
        if (animation) {
            if (ImGui::Button(animation->isPlaying(entity->id()) ? "Pause Preview" : "Play Preview")) {
                if (animation->isPlaying(entity->id())) { animation->stop(entity->id()); }
                else { (void)animation->resume(entity->id()); }
            }
            float time = animation->time(entity->id());
            float duration = 1;
            AnimationClipAsset clip;
            if (isAnimationClipAssetPath(animator.clip)) {
                std::string bytes, error;
                if (fileAssetReader(m_impl->project->root())(animator.clip, bytes, error) == AssetReadResult::Found && decodeAnimationClip(bytes, clip, error)) { duration = clip.endSeconds - clip.startSeconds; }
            } else { duration = std::max(1.0f, time + 1); }
            if (ImGui::SliderFloat("Time", &time, 0, duration)) { (void)animation->setTime(entity->id(), time); }
            if (!animation->error().empty()) { ImGui::TextWrapped("%s", animation->error().c_str()); }
        }
    }
    if (entity->hasAudioSource && componentHeader("Audio Source", [&] {
        entity->hasAudioSource = false;
        entity->audioSource = {};
    }, {entity->audioSource.clipPath})) {
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
    }
    if (entity->hasLight && componentHeader("Light", [&] {
        entity->hasLight = false;
        entity->light = {};
    })) {
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
    }
    if (entity->hasAudioListener) {
        componentHeader("Audio Listener", [&] { entity->hasAudioListener = false; });
    }
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) {
        m_impl->refreshScripts();
        m_impl->m_scriptSearch.front() = '\0';
        ImGui::OpenPopup("AddComponent");
    }
    if (ImGui::BeginPopup("AddComponent")) {
        if (ImGui::MenuItem("Game UI", nullptr, false, !entity->gameUi)) {
            entity->gameUi.emplace();
            changed = true;
        }
        if (ImGui::MenuItem("Light", nullptr, false, !entity->hasLight)) {
            entity->hasLight = true;
            entity->light = {};
            changed = true;
        }
        if (ImGui::MenuItem("Animator", nullptr, false, !entity->hasAnimator && !animatorMeshPath(*m_impl->scene, *entity).empty())) {
            entity->hasAnimator = true; entity->animator = {}; changed = true;
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
            auto& search = m_impl->m_scriptSearch;
            if (!m_impl->availableScripts.empty()) {
                if (ImGui::IsWindowAppearing()) {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
                ImGui::InputTextWithHint("##ScriptSearch", "Search scripts", search.data(), search.size());
            }
            bool hasMatches = false;
            for (const auto& path : m_impl->availableScripts) {
                if (search.front() != '\0' && !ImStristr(path.data(), path.data() + path.size(), search.data(), nullptr)) {
                    continue;
                }
                hasMatches = true;
                const bool attached = std::any_of(entity->scripts.begin(), entity->scripts.end(),
                    [&](const ScriptComponent& component) { return component.path == path; });
                if (ImGui::MenuItem(path.c_str(), nullptr, attached, !attached)) {
                    const bool added = m_impl->scene->attachScript(static_cast<std::size_t>(selectedIndex), path);
                    (void)added; // Valid path and duplicate checks were applied above.
                }
            }
            if (!hasMatches && !m_impl->availableScripts.empty()) {
                ImGui::TextDisabled("No matching scripts.");
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
