#include "AssetImportPanel.hpp"

#include "AssetLibrary.hpp"
#include "Project.hpp"
#include "Renderer.hpp"

#include <imgui.h>

#include <filesystem>
#include <utility>

namespace sat {
AssetImportPanel::AssetImportPanel(Project& project, Renderer& renderer) : m_project(project), m_renderer(renderer) {}
void AssetImportPanel::select(std::string path) {
    m_path = std::move(path);
    revert();
}
void AssetImportPanel::clear() { m_path.clear(); m_error.clear(); m_info.clear(); m_saved = {}; m_edited = {}; m_valid = true; }
void AssetImportPanel::revert() {
    AssetImportSettings saved;
    std::string bytes;
    m_error.clear();
    const auto result = fileAssetReader(m_project.root())(m_path + ".satimport", bytes, m_error);
    m_valid = result != AssetReadResult::Error && (result == AssetReadResult::Missing || parseImportSettings(bytes, saved, m_error));
    m_saved = saved;
    m_edited = saved;
    m_info = m_renderer.assetInfo(m_path);
    if (m_error.empty() && m_info.empty()) {
        for (const auto& error : m_renderer.assetErrors()) {
            if (error.starts_with(m_path + ": ")) { m_error = error; break; }
        }
    }
}
bool AssetImportPanel::apply() {
    const auto source = m_project.root() / std::filesystem::path(std::u8string(m_path.begin(), m_path.end()));
    if (!m_renderer.reimportAsset(m_path, &m_edited,
        [&](const AssetImportSettings& settings, std::string& error) { return saveImportSettings(source, settings, error); }, m_error)) { return false; }
    m_saved = m_edited;
    m_valid = true;
    m_info = m_renderer.assetInfo(m_path);
    return true;
}
void AssetImportPanel::request(std::function<void()> action) {
    if (m_pending) { return; }
    if (!dirty()) { action(); return; }
    m_pending = std::move(action);
    m_openPrompt = true;
}
void AssetImportPanel::draw(bool readOnly) {
    ImGui::Begin("Inspector");
    ImGui::TextWrapped("%s", m_path.c_str());
    const bool mesh = isMeshAssetPath(m_path);
    ImGui::TextUnformatted(mesh ? "Mesh Import Settings" : "Texture Import Settings");
    ImGui::Separator();
    ImGui::BeginDisabled(readOnly);
    if (mesh) {
        ImGui::DragFloat("Scale", &m_edited.scale, 0.01f);
        int handedness = m_edited.rightHanded ? 0 : 1;
        if (ImGui::Combo("Source coordinates", &handedness, "Right-handed\0Left-handed\0")) { m_edited.rightHanded = handedness == 0; }
        ImGui::Checkbox("Flip UV V", &m_edited.flipV);
    } else {
        int size = 0;
        for (int value = 32; value < m_edited.maximumSize; value *= 2) { ++size; }
        if (ImGui::Combo("Maximum size", &size, "32\0" "64\0" "128\0" "256\0" "512\0" "1024\0" "2048\0" "4096\0")) { m_edited.maximumSize = 32 << size; }
        int filter = static_cast<int>(m_edited.filter);
        if (ImGui::Combo("Filter", &filter, "Point\0Bilinear\0Trilinear\0")) { m_edited.filter = static_cast<TextureFilter>(filter); }
        int wrap = static_cast<int>(m_edited.wrap);
        if (ImGui::Combo("Wrap", &wrap, "Repeat\0Clamp\0")) { m_edited.wrap = static_cast<TextureWrap>(wrap); }
        ImGui::Checkbox("Generate mipmaps", &m_edited.mipmaps);
    }
    ImGui::BeginDisabled(!dirty() && m_valid);
    if (ImGui::Button("Apply")) { (void)apply(); }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) { revert(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(dirty() || !m_valid);
    if (ImGui::Button("Reimport")) {
        std::string error;
        const bool success = m_renderer.reimportAsset(m_path, nullptr, {}, error);
        revert(); // Refresh saved controls too, including externally edited sidecars.
        if (!success) { m_error = std::move(error); }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (dirty()) { ImGui::TextUnformatted("Unapplied changes"); }
    if (!m_info.empty()) { ImGui::TextUnformatted(m_info.c_str()); }
    if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
    if (readOnly) { ImGui::TextDisabled("Import settings cannot change during Play."); }
    ImGui::End();
}
void AssetImportPanel::drawPrompt() {
    if (m_openPrompt) { ImGui::OpenPopup("Unapplied Import Settings"); m_openPrompt = false; }
    std::function<void()> action;
    if (ImGui::BeginPopupModal("Unapplied Import Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Apply changes to %s before continuing?", m_path.c_str());
        if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
        if (ImGui::Button("Apply")) {
            if (apply()) { action = std::move(m_pending); m_pending = {}; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) { revert(); action = std::move(m_pending); m_pending = {}; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { m_pending = {}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (action) { action(); }
}
}
