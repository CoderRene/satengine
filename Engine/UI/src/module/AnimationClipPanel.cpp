#include "AssetImportPanel.hpp"
#include "AssetWrite.hpp"
#include "Project.hpp"
#include "Renderer.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_set>

namespace sat {
bool AssetImportPanel::saveAnimationClip() {
    std::string bytes;
    if (!encodeAnimationClip(m_animationClip, bytes, m_error) ||
        !writeAsset(m_project.root() / std::filesystem::u8path(m_path), m_path, bytes, m_error)) { return false; }
    m_clipDirty = false; m_renderer.invalidateAsset(m_path); return true;
}
void AssetImportPanel::drawAnimationClip(bool readOnly) {
    ImGui::BeginDisabled(readOnly || !m_valid);
    std::array<char, 4097> name{};
    std::copy_n(m_animationClip.clip.name.data(), std::min(m_animationClip.clip.name.size(), name.size() - 1), name.data());
    if (ImGui::InputText("Name", name.data(), name.size())) { m_animationClip.clip.name = name.data(); m_clipDirty = true; }
    m_clipDirty |= ImGui::DragFloat("Start", &m_animationClip.startSeconds, 0.01f, 0, m_animationClip.clip.duration);
    m_clipDirty |= ImGui::DragFloat("End", &m_animationClip.endSeconds, 0.01f, 0, m_animationClip.clip.duration);
    m_clipDirty |= ImGui::Checkbox("Loop", &m_animationClip.loop);
    m_clipDirty |= ImGui::InputFloat("Speed", &m_animationClip.speed);
    if (ImGui::Button("Save")) { (void)saveAnimationClip(); }
    ImGui::EndDisabled(); ImGui::SameLine(); if (ImGui::Button("Reload")) { revert(); }
    if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
}
void AssetImportPanel::extractAnimations(bool all) {
    m_animationExtraction.clear();
    std::unordered_set<std::string> paths;
    const auto source = std::filesystem::u8path(m_path);
    bool overwrite = false;
    for (std::size_t index = 0; index < m_animations.clips.size(); ++index) {
        if (!all && index != static_cast<std::size_t>(m_animationIndex)) { continue; }
        AnimationClipAsset asset;
        asset.clip = m_animations.clips[index]; asset.endSeconds = asset.clip.duration;
        for (const auto& joint : m_animations.joints) { asset.jointNames.push_back(joint.name); }
        std::string name = asset.clip.name;
        for (char& ch : name) { if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) { ch = '_'; } }
        if (name.empty()) { name = "Clip"; }
        const auto stem = source.stem().string() + "_" + name;
        std::string path = (source.parent_path() / (stem + ".satanim")).generic_string();
        unsigned suffix = 1;
        while (!paths.insert(path).second) { path = (source.parent_path() / (stem + "_" + std::to_string(suffix++) + ".satanim")).generic_string(); }
        std::string bytes;
        if (!encodeAnimationClip(asset, bytes, m_error)) { m_animationExtraction.clear(); return; }
        std::error_code error;
        overwrite |= std::filesystem::exists(m_project.root() / std::filesystem::u8path(path), error);
        if (error) { m_error = error.message(); m_animationExtraction.clear(); return; }
        m_animationExtraction.emplace_back(std::move(path), std::move(bytes));
    }
    if (overwrite) { ImGui::OpenPopup("Overwrite Animation Clips"); }
    else { saveAnimationExtraction(); }
}
void AssetImportPanel::saveAnimationExtraction() {
    struct Backup { bool exists = false; std::string bytes; };
    std::vector<Backup> backups(m_animationExtraction.size());
    const auto reader = fileAssetReader(m_project.root());
    for (std::size_t index = 0; index < backups.size(); ++index) {
        const auto result = reader(m_animationExtraction[index].first, backups[index].bytes, m_error);
        if (result == AssetReadResult::Error) { return; }
        backups[index].exists = result == AssetReadResult::Found;
    }
    std::size_t written = 0;
    for (; written < m_animationExtraction.size(); ++written) {
        const auto& [path, bytes] = m_animationExtraction[written];
        if (!writeAsset(m_project.root() / std::filesystem::u8path(path), path, bytes, m_error)) { break; }
        m_renderer.invalidateAsset(path);
    }
    if (written != m_animationExtraction.size()) {
        while (written > 0) {
            --written;
            const auto& path = m_animationExtraction[written].first;
            const auto file = m_project.root() / std::filesystem::u8path(path);
            std::string rollback;
            if (backups[written].exists) {
                if (!writeAsset(file, path, backups[written].bytes, rollback)) { m_error += "\nRollback failed: " + rollback; }
            } else {
                std::error_code error; std::filesystem::remove(file, error);
                if (error) { m_error += "\nRollback failed: " + error.message(); }
            }
        }
    }
    m_animationExtraction.clear();
}
void AssetImportPanel::drawAnimations() {
    if (!ImGui::CollapsingHeader("Animations")) { return; }
    if (!m_animationsLoaded) {
        m_animationsLoaded = true;
        const auto reader = fileAssetReader(m_project.root()); std::string bytes;
        if (reader(m_path, bytes, m_error) == AssetReadResult::Found) { (void)decodeGltfAnimation(bytes, m_path, reader, m_animations, m_error); }
    }
    for (std::size_t index = 0; index < m_animations.clips.size(); ++index) {
        const auto& clip = m_animations.clips[index]; ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(clip.name.c_str(), m_animationIndex == static_cast<int>(index))) { m_animationIndex = static_cast<int>(index); }
        ImGui::TextDisabled("%.3f seconds, %zu channels", clip.duration, clip.channels.size()); ImGui::PopID();
    }
    ImGui::BeginDisabled(m_animations.clips.empty());
    if (ImGui::Button(m_previewAnimation.isPlaying(m_previewEntity) ? "Pause Preview" : "Play Preview")) {
        if (m_previewAnimation.isPlaying(m_previewEntity)) { m_previewAnimation.stop(m_previewEntity); }
        else {
            if (!m_previewEntity) {
                auto& entity = m_previewScene.createEntity("Animation Preview");
                entity.hasMesh = entity.hasAnimator = true; entity.mesh.meshPath = m_path;
                m_previewEntity = entity.id(); m_previewAnimation.setAssetReader(fileAssetReader(m_project.root()));
            }
            auto* entity = m_previewScene.entityById(m_previewEntity);
            entity->animator.clip = m_animations.clips[m_animationIndex].name;
            (void)m_previewAnimation.play(m_previewEntity, entity->animator.clip, true, 0);
        }
    }
    if (m_previewEntity) {
        m_previewAnimation.update(ImGui::GetIO().DeltaTime);
        const auto preview = m_renderer.assetThumbnail(m_path, m_previewAnimation.palette(m_previewEntity));
        if (preview.texture) { ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)), ImVec2(192, 192)); }
    }
    if (ImGui::Button("Extract Clip")) { extractAnimations(false); }
    ImGui::SameLine(); if (ImGui::Button("Extract All Clips")) { extractAnimations(true); }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Overwrite Animation Clips", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Replace existing animation clips?");
        for (const auto& entry : m_animationExtraction) { ImGui::TextUnformatted(entry.first.c_str()); }
        if (ImGui::Button("Overwrite")) { saveAnimationExtraction(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { m_animationExtraction.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
}
