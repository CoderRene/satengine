#include "AssetImportPanel.hpp"
#include "CursorWrap.hpp"

#include "AssetLibrary.hpp"
#include "Project.hpp"
#include "Renderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

namespace sat {
AssetImportPanel::AssetImportPanel(Project& project, Renderer& renderer) : m_project(project), m_renderer(renderer) {}
void AssetImportPanel::select(std::string path) {
    clear();
    m_path = std::move(path);
    revert();
}
void AssetImportPanel::clear() { m_path.clear(); m_error.clear(); m_info.clear(); m_saved = {}; m_edited = {}; m_valid = true; m_material = {}; m_savedMaterial = {}; m_texturePaths.clear(); m_shaderPaths.clear(); }
void AssetImportPanel::revert() {
    AssetImportSettings saved;
    std::string bytes;
    m_error.clear();
    if (isMaterialAssetPath(m_path)) {
        const auto result = fileAssetReader(m_project.root())(m_path, bytes, m_error);
        m_savedMaterial = {};
        m_valid = result == AssetReadResult::Found && decodeMaterial(bytes, m_savedMaterial, m_error);
        if (result == AssetReadResult::Missing) { m_error = "Material file is missing"; }
        m_material = m_savedMaterial;
        m_texturePaths.clear();
        m_shaderPaths.clear();
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_project.root() / "Assets", ec);
             !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {
            if (!it->is_regular_file(ec)) { continue; }
            const auto encoded = it->path().lexically_relative(m_project.root()).generic_u8string();
            std::string path(encoded.begin(), encoded.end());
            if (isTextureAssetPath(path)) { m_texturePaths.push_back(std::move(path)); }
            else if (isShaderAssetPath(path) && !it->path().filename().u8string().starts_with(u8"._")) {
                m_shaderPaths.push_back(std::move(path));
            }
        }
        std::sort(m_texturePaths.begin(), m_texturePaths.end());
        std::sort(m_shaderPaths.begin(), m_shaderPaths.end());
        return;
    }
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
    if (isMaterialAssetPath(m_path)) {
        const auto bytes = encodeMaterial(m_material);
        MaterialData validated;
        if (!decodeMaterial(bytes, validated, m_error)) { return false; }
        auto temp = source;
        temp += ".tmp";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << bytes;
        out.close();
        std::error_code ec;
        if (out) { std::filesystem::rename(temp, source, ec); }
        if (!out || ec) {
            m_error = "Cannot save material " + m_path + (ec ? ": " + ec.message() : "");
            std::filesystem::remove(temp, ec);
            return false;
        }
        m_savedMaterial = m_material = std::move(validated);
        m_renderer.invalidateMaterial(m_path);
        m_valid = true;
        return true;
    }
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
void AssetImportPanel::drawMaterial(bool readOnly) {
    ImGui::BeginDisabled(readOnly);
    const char* shaderLabel = m_material.shader == MaterialShader::Custom ? m_material.shaderPath.c_str() :
        (m_material.shader == MaterialShader::Unlit ? "Unlit" : "Lit (PBR)");
    if (ImGui::BeginCombo("Shader", shaderLabel)) {
        if (ImGui::Selectable("Lit (PBR)", m_material.shader == MaterialShader::Lit)) {
            m_material.shader = MaterialShader::Lit;
            m_material.shaderPath.clear();
        }
        if (ImGui::Selectable("Unlit", m_material.shader == MaterialShader::Unlit)) {
            m_material.shader = MaterialShader::Unlit;
            m_material.shaderPath.clear();
        }
        for (const auto& path : m_shaderPaths) {
            if (ImGui::Selectable(path.c_str(), m_material.shaderPath == path)) {
                m_material.shader = MaterialShader::Custom;
                m_material.shaderPath = path;
            }
        }
        ImGui::EndCombo();
    }
    int alphaMode = static_cast<int>(m_material.alphaMode);
    if (ImGui::Combo("Alpha Mode", &alphaMode, "Opaque\0Mask\0Blend\0")) {
        m_material.alphaMode = static_cast<AlphaMode>(alphaMode);
    }
    if (m_material.alphaMode == AlphaMode::Mask) {
        ImGui::SliderFloat("Alpha Cutoff", &m_material.alphaCutoff, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    ImGui::ColorEdit4("Base Color", m_material.baseColor);
    ImGui::BeginDisabled(m_material.shader == MaterialShader::Unlit);
    ImGui::SliderFloat("Metallic", &m_material.metallic, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Roughness", &m_material.roughness, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Specular Strength", &m_material.specularStrength, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("Emissive", m_material.emissive);
    ImGui::SliderFloat("Normal Scale", &m_material.normalScale, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    const auto picker = [&](const char* label, std::string& path) {
        if (ImGui::BeginCombo(label, path.empty() ? "Default" : path.c_str())) {
            if (ImGui::Selectable("Default", path.empty())) { path.clear(); }
            for (const auto& candidate : m_texturePaths) {
                if (ImGui::Selectable(candidate.c_str(), path == candidate)) { path = candidate; }
            }
            ImGui::EndCombo();
        }
    };
    ImGui::EndDisabled();
    picker("Base Color Texture", m_material.baseColorTexture);
    ImGui::BeginDisabled(m_material.shader == MaterialShader::Unlit);
    picker("Metallic-Roughness Texture", m_material.metallicRoughnessTexture);
    picker("Normal Texture", m_material.normalTexture);
    picker("Emissive Texture", m_material.emissiveTexture);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!dirty() && m_valid);
    if (ImGui::Button("Save Material")) { (void)apply(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) { revert(); m_renderer.invalidateMaterial(m_path); }
    ImGui::EndDisabled();
    if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
}
void AssetImportPanel::draw(bool readOnly) {
    ImGui::Begin("Inspector");
    ImGui::TextWrapped("%s", m_path.c_str());
    if (isMaterialAssetPath(m_path)) {
        drawMaterial(readOnly);
        ImGui::End();
        return;
    }
    const bool mesh = isMeshAssetPath(m_path);
    ImGui::TextUnformatted(mesh ? "Mesh Import Settings" : "Texture Import Settings");
    ImGui::Separator();
    ImGui::BeginDisabled(readOnly);
    if (mesh) {
        dragFloat("Scale", &m_edited.scale, 0.01f);
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
