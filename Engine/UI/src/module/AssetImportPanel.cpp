#include "AssetImportPanel.hpp"
#include "AssetWrite.hpp"
#include "ContentDragDrop.hpp"
#include "CursorWrap.hpp"

#include "AssetLibrary.hpp"
#include "Project.hpp"
#include "Renderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sat {
namespace {
bool assignDroppedAsset(std::string& path, bool (*matches)(std::string_view)) {
    if ((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) || !ImGui::BeginDragDropTarget()) {
        return false;
    }
    const auto dragged = draggedContentPath(ImGui::GetDragDropPayload());
    const bool assigned = matches(dragged) && ImGui::AcceptDragDropPayload(kContentFilePayload);
    if (assigned) {
        path = dragged;
    }
    ImGui::EndDragDropTarget();
    return assigned;
}

std::filesystem::path assetFile(const Project& project, std::string_view path) {
    return project.root() / std::filesystem::path(std::u8string(path.begin(), path.end()));
}
void collectObjMaterialNames(const std::vector<ObjMaterial>& materials, std::vector<std::string>& names) {
    names.reserve(materials.size());
    for (const auto& material : materials) { names.push_back(material.name); }
}
void readMeshMaterialNames(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::vector<std::string>& names, std::vector<std::string>& textures, std::string& error) {
    if (isGltfAssetPath(path)) {
        (void)gltfMaterialNames(bytes, names, error, &textures);
    } else {
        std::vector<ObjMaterial> materials;
        if (decodeObjMaterials(bytes, path, reader, materials, error)) { collectObjMaterialNames(materials, names); }
    }
}
bool fileEntryExists(const std::filesystem::path& file, std::error_code& error) {
    const auto status = std::filesystem::symlink_status(file, error);
    if (error == std::errc::no_such_file_or_directory) { error.clear(); }
    return std::filesystem::exists(status);
}
}
AssetImportPanel::AssetImportPanel(Project& project, Renderer& renderer) : m_project(project), m_renderer(renderer) {}
std::string AssetImportPanel::updateMaterialReferences(std::string_view oldPath, std::string_view newPath) {
    if (!isTextureAssetPath(oldPath) && !isShaderAssetPath(oldPath)) { return {}; }
    const auto reader = fileAssetReader(m_project.root());
    const auto assets = m_project.root() / "Assets";
    std::error_code ec;
    std::string firstError;
    for (auto it = std::filesystem::recursive_directory_iterator(assets, ec);
         !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {
        if (!std::filesystem::is_regular_file(it->symlink_status(ec))) { continue; }
        const auto encoded = it->path().lexically_relative(m_project.root()).generic_u8string();
        const std::string path(encoded.begin(), encoded.end());
        if (!isMaterialAssetPath(path)) { continue; }

        std::string bytes;
        std::string error;
        MaterialData material;
        if (reader(path, bytes, error) != AssetReadResult::Found || !decodeMaterial(bytes, material, error)) {
            if (firstError.empty()) { firstError = "Could not check material references in " + path; }
            continue;
        }
        bool changed = false;
        for (auto* reference : {&material.baseColorTexture, &material.metallicRoughnessTexture,
                &material.normalTexture, &material.emissiveTexture, &material.occlusionTexture,
                &material.transmissionTexture, &material.thicknessTexture, &material.clearcoatTexture,
                &material.clearcoatRoughnessTexture, &material.clearcoatNormalTexture, &material.shaderPath}) {
            if (*reference == oldPath) {
                *reference = newPath;
                changed = true;
            }
        }
        if (changed && !writeAsset(assetFile(m_project, path), path, encodeMaterial(material), error) && firstError.empty()) {
            firstError = std::move(error);
        }
    }
    if (ec && firstError.empty()) { firstError = "Could not scan materials: " + ec.message(); }
    return firstError;
}

void AssetImportPanel::select(std::string path) {
    clear();
    m_path = std::move(path);
    revert();
}
void AssetImportPanel::clear() {
    m_previewAnimation.stop(); m_previewScene.clear(); m_previewEntity = kInvalidEntityId;
    m_animations = {}; m_animationsLoaded = false; m_clipDirty = false; m_animationExtraction.clear();
    m_meshInfoPending = false;
    m_meshInfoObsolete = true;
    m_path.clear();
    m_error.clear();
    m_info.clear();
    m_saved = {};
    m_edited = {};
    m_valid = true;
    m_material = {};
    m_savedMaterial = {};
    m_texturePaths.clear();
    m_shaderPaths.clear();
    m_materialNames.clear();
    m_textureNames.clear();
    m_materialStatus.clear();
    m_materialIndex = 0;
    m_materialExtraction.reset();
}
void AssetImportPanel::revert() {
    if (isAnimationClipAssetPath(m_path)) {
        std::string bytes;
        m_valid = fileAssetReader(m_project.root())(m_path, bytes, m_error) == AssetReadResult::Found && decodeAnimationClip(bytes, m_animationClip, m_error);
        m_clipDirty = false;
        return;
    }
    if (isMeshAssetPath(m_path)) {
        m_meshInfoPending = true;
        m_meshInfoObsolete = true;
        return;
    }
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
    const auto reader = fileAssetReader(m_project.root());
    const auto result = reader(m_path + ".satimport", bytes, m_error);
    m_valid = result != AssetReadResult::Error && (result == AssetReadResult::Missing || parseImportSettings(bytes, saved, m_error));
    m_saved = saved;
    m_edited = saved;
    const bool texture = isTextureAssetPath(m_path);
    m_info.clear();
    if (texture && m_valid) {
        TextureInfo info;
        if (inspectTextureFile(m_project.root(), m_path, saved, info, m_error)) {
            m_info = std::to_string(info.width) + " x " + std::to_string(info.height) + ", " +
                std::to_string(info.mipLevels) + " mip levels";
        }
    }
    if (m_error.empty() && (texture || m_info.empty())) {
        for (const auto& error : m_renderer.assetErrors()) {
            if (error.starts_with(m_path + ": ")) { m_error = error; break; }
        }
    }
}
void AssetImportPanel::pollMeshInfo() {
    using namespace std::chrono_literals;
    if (m_meshInfo.valid()) {
        if (m_meshInfo.wait_for(0s) != std::future_status::ready) { return; }
        try {
            auto result = m_meshInfo.get();
            if (!m_meshInfoObsolete) {
                m_saved = m_edited = result.settings;
                m_valid = result.validSettings;
                m_info = std::move(result.info);
                m_error = std::move(result.error);
                m_materialNames = std::move(result.materials);
                m_textureNames = std::move(result.textureNames);
                m_materialStatus = std::move(result.materialError);
                if (m_materialIndex >= m_materialNames.size()) { m_materialIndex = 0; }
            }
        } catch (const std::exception& error) {
            if (!m_meshInfoObsolete) { m_error = "Cannot read mesh: " + std::string(error.what()); }
        }
    }
    if (!m_meshInfoPending) { return; }
    m_meshInfoPending = false;
    try {
        m_meshInfo = std::async(std::launch::async, [reader = fileAssetReader(m_project.root()), path = m_path] {
            MeshInfo result;
            std::string bytes;
            const auto metadata = reader(path + ".satimport", bytes, result.error);
            result.validSettings = metadata == AssetReadResult::Missing ||
                (metadata == AssetReadResult::Found && parseImportSettings(bytes, result.settings, result.error));
            if (!result.validSettings) { return result; }
            const AssetReader readMesh = [&](std::string_view file, std::string& output, std::string& error) {
                const auto read = reader(file, output, error);
                if (read == AssetReadResult::Found && file == path) {
                    readMeshMaterialNames(output, path, reader, result.materials, result.textureNames, result.materialError);
                }
                return read;
            };
            PreparedAssetImport asset;
            if (decodeAssetImport(readMesh, path, &result.settings, asset, result.error)) {
                result.info = std::to_string(asset.mesh.vertices.size()) + " vertices, " +
                    std::to_string(asset.mesh.indices.size() / 3) + " triangles";
            }
            return result;
        });
        m_meshInfoObsolete = false;
    } catch (const std::exception& error) {
        m_error = "Cannot start reading mesh: " + std::string(error.what());
    }
}

void AssetImportPanel::startImport(bool reimport) {
    if (m_import.valid()) { return; }
    m_error.clear();
    try {
        m_import = std::async(std::launch::async,
            [reader = fileAssetReader(m_project.root()), source = assetFile(m_project, m_path),
             path = m_path, settings = m_edited, material = m_material, reimport] {
                ImportResult result;
                if (isMaterialAssetPath(path)) {
                    const auto bytes = encodeMaterial(material);
                    MaterialData validated;
                    if (decodeMaterial(bytes, validated, result.error) && writeAsset(source, path, bytes, result.error)) {
                        result.material = std::move(validated);
                    }
                } else if (decodeAssetImport(reader, path, reimport ? nullptr : &settings, result.asset, result.error) &&
                    reimport && isMeshAssetPath(path)) {
                    std::string bytes;
                    if (reader(path, bytes, result.materialStatus) == AssetReadResult::Found) {
                        readMeshMaterialNames(bytes, path, reader, result.materialNames, result.textureNames, result.materialStatus);
                    } else if (result.materialStatus.empty()) {
                        result.materialStatus = "Cannot read model source";
                    }
                }
                return result;
            });
    } catch (const std::exception& error) {
        m_error = "Cannot start asset import: " + std::string(error.what());
        return;
    }
    m_reimport = reimport;
    m_importShown = false;
    if (!m_pending) { m_openPrompt = true; }
}

bool AssetImportPanel::finishImport() {
    ImportResult result;
    try {
        result = m_import.get();
    } catch (const std::exception& error) {
        m_error = "Asset import failed: " + std::string(error.what());
        return false;
    }
    if (result.material) {
        m_savedMaterial = m_material = std::move(*result.material);
        m_renderer.invalidateMaterial(m_path);
        m_valid = true;
        return true;
    }
    if (result.asset.path.empty()) {
        m_error = result.error.empty() ? "Asset import failed" : std::move(result.error);
        return false;
    }

    const auto settings = result.asset.settings;
    AssetImportWriter writer;
    if (!m_reimport) {
        writer = [source = assetFile(m_project, m_path)](const AssetImportSettings& imported, std::string& error) {
            return saveImportSettings(source, imported, error);
        };
    }
    if (!m_renderer.publishAssetImport(std::move(result.asset), writer, m_error)) { return false; }
    m_saved = m_edited = settings;
    m_valid = true;
    m_info = m_renderer.assetInfo(m_path);
    if (m_reimport && isMeshAssetPath(m_path)) {
        m_materialNames = std::move(result.materialNames);
        m_textureNames = std::move(result.textureNames);
        m_materialStatus = std::move(result.materialStatus);
        if (m_materialIndex >= m_materialNames.size()) { m_materialIndex = 0; }
    }
    return true;
}
void AssetImportPanel::request(std::function<void()> action) {
    if (m_pending || m_import.valid()) { return; }
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
    } else if (assignDroppedAsset(m_material.shaderPath, isShaderAssetPath)) {
        m_material.shader = MaterialShader::Custom;
    }
    int alphaMode = static_cast<int>(m_material.alphaMode);
    if (ImGui::Combo("Alpha Mode", &alphaMode, "Opaque\0Mask\0Blend\0")) {
        m_material.alphaMode = static_cast<AlphaMode>(alphaMode);
    }
    if (m_material.alphaMode == AlphaMode::Mask) {
        ImGui::SliderFloat("Alpha Cutoff", &m_material.alphaCutoff, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    ImGui::Checkbox("Double Sided", &m_material.doubleSided);
    ImGui::ColorEdit4("Base Color", m_material.baseColor);
    ImGui::BeginDisabled(m_material.shader == MaterialShader::Unlit);
    ImGui::SliderFloat("Metallic", &m_material.metallic, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Roughness", &m_material.roughness, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Specular Strength", &m_material.specularStrength, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("Emissive", m_material.emissive);
    ImGui::SliderFloat("Normal Scale", &m_material.normalScale, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (!m_material.occlusionTexture.empty()) {
        ImGui::SliderFloat("Occlusion Strength", &m_material.occlusionStrength, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    const auto picker = [&](const char* label, std::string& path) {
        if (ImGui::BeginCombo(label, path.empty() ? "Default" : path.c_str())) {
            if (ImGui::Selectable("Default", path.empty())) { path.clear(); }
            for (const auto& candidate : m_texturePaths) {
                if (ImGui::Selectable(candidate.c_str(), path == candidate)) { path = candidate; }
            }
            ImGui::EndCombo();
        } else {
            assignDroppedAsset(path, isTextureAssetPath);
        }
    };
    ImGui::EndDisabled();
    picker("Base Color Texture", m_material.baseColorTexture);
    ImGui::BeginDisabled(m_material.shader == MaterialShader::Unlit);
    picker("Metallic-Roughness Texture", m_material.metallicRoughnessTexture);
    picker("Normal Texture", m_material.normalTexture);
    picker("Occlusion Texture", m_material.occlusionTexture);
    picker("Emissive Texture", m_material.emissiveTexture);
    ImGui::SliderFloat("Clearcoat", &m_material.clearcoat, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (m_material.clearcoat > 0.0f) {
        picker("Clearcoat Texture", m_material.clearcoatTexture);
        ImGui::SliderFloat("Clearcoat Roughness", &m_material.clearcoatRoughness, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        picker("Clearcoat Roughness Texture", m_material.clearcoatRoughnessTexture);
        picker("Clearcoat Normal Texture", m_material.clearcoatNormalTexture);
        if (!m_material.clearcoatNormalTexture.empty()) {
            ImGui::DragFloat("Clearcoat Normal Scale", &m_material.clearcoatNormalScale, 0.01f);
        }
    }
    ImGui::SliderFloat("Transmission", &m_material.transmission, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (m_material.transmission > 0.0f) {
        picker("Transmission Texture", m_material.transmissionTexture);
        ImGui::SliderFloat("Transmission Mip", &m_material.transmissionMip, 0, kMaxTransmissionMip, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Roughness Blur Strength", &m_material.transmissionRoughnessStrength, 0, 1, "%.2f",
            ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Index of Refraction", &m_material.ior, 0.01f, 1.0f, 0.0f, "%.3f");
        ImGui::DragFloat("Thickness", &m_material.thickness, 0.01f, 0.0f, 0.0f, "%.3f");
        m_material.thickness = std::max(m_material.thickness, 0.0f);
        if (m_material.thickness > 0.0f) {
            picker("Thickness Texture", m_material.thicknessTexture);
            ImGui::ColorEdit3("Attenuation Color", m_material.attenuationColor);
            ImGui::DragFloat("Attenuation Distance", &m_material.attenuationDistance, 0.01f, 0.0f, 0.0f, "%.3f");
            m_material.attenuationDistance = std::max(m_material.attenuationDistance, 0.0f);
        }
        if (m_material.ior != 0.0f) { m_material.ior = std::max(m_material.ior, 1.0f); }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!dirty() && m_valid);
    if (ImGui::Button("Save Material")) { startImport(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) { revert(); m_renderer.invalidateMaterial(m_path); }
    ImGui::EndDisabled();
    if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
}
void AssetImportPanel::requestMaterialExtraction(bool all) {
    m_materialStatus.clear();
    const auto reader = fileAssetReader(m_project.root());
    std::string bytes;
    if (reader(m_path, bytes, m_materialStatus) != AssetReadResult::Found) {
        if (m_materialStatus.empty()) { m_materialStatus = "Cannot read model source"; }
        return;
    }

    const bool gltf = isGltfAssetPath(m_path);
    std::vector<std::string> names;
    std::vector<ObjMaterial> objMaterials;
    if (gltf) {
        if (!gltfMaterialNames(bytes, names, m_materialStatus)) { return; }
    } else {
        if (!decodeObjMaterials(bytes, m_path, reader, objMaterials, m_materialStatus)) { return; }
        collectObjMaterialNames(objMaterials, names);
    }
    if (names != m_materialNames) {
        m_materialNames = names;
        m_materialIndex = 0;
        if (!all) { m_materialStatus = "Source materials changed. Select a material to extract."; return; }
    }
    if (names.empty()) { m_materialStatus = "This model has no materials."; return; }

    MaterialExtraction extraction;
    auto paths = meshMaterialPaths(m_path, names);
    if (all) {
        extraction.paths = std::move(paths);
        if (gltf) {
            if (!decodeGltfMaterials(bytes, m_path, reader, extraction.materials, extraction.images, m_materialStatus)) { return; }
        } else {
            extraction.materials.reserve(objMaterials.size());
            for (auto& material : objMaterials) { extraction.materials.push_back(std::move(material.material)); }
        }
    } else {
        extraction.paths.push_back(std::move(paths[m_materialIndex]));
        if (gltf) {
            extraction.materials.resize(1);
            if (!decodeGltfMaterial(bytes, m_path, reader, m_materialIndex,
                extraction.materials.front(), extraction.images, m_materialStatus)) { return; }
        } else {
            extraction.materials.push_back(std::move(objMaterials[m_materialIndex].material));
        }
    }

    std::error_code ec;
    bool needsConfirmation = false;
    for (const auto& path : extraction.paths) {
        if (!isMaterialAssetPath(path)) { m_materialStatus = "Invalid material destination: " + path; return; }
        const bool exists = fileEntryExists(assetFile(m_project, path), ec);
        if (ec) { m_materialStatus = "Cannot inspect " + path + ": " + ec.message(); return; }
        extraction.overwrite.push_back(exists);
        needsConfirmation |= exists;
    }
    if (needsConfirmation) {
        m_materialExtraction = std::move(extraction);
        ImGui::OpenPopup("Overwrite Material");
        return;
    }
    saveMaterialExtraction(extraction);
}
void AssetImportPanel::saveMaterialExtraction(MaterialExtraction& extraction) {
    const auto reader = fileAssetReader(m_project.root());
    std::string bytes;
    std::vector<const GltfImage*> pending;
    std::unordered_map<std::string, std::string_view> renamed;
    const auto pathKey = [](std::string path) {
        for (auto& ch : path) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch + ('a' - 'A')); }
        }
        return path;
    };
    std::unordered_set<std::string> reserved;
    reserved.reserve(extraction.images.size());
    for (const auto& image : extraction.images) { reserved.insert(pathKey(image.path)); }
    std::error_code ec;
    for (auto& image : extraction.images) {
        const auto originalPath = image.path;
        const auto dot = originalPath.find_last_of('.');
        std::size_t suffix = 0;
        bool reuse = false;
        while (fileEntryExists(assetFile(m_project, image.path), ec)) {
            std::string readError;
            if (reader(image.path, bytes, readError) == AssetReadResult::Found && bytes == image.bytes) {
                reuse = true;
                break;
            }
            do {
                image.path = originalPath.substr(0, dot) + "_" + std::to_string(++suffix) + originalPath.substr(dot);
                if (!isTextureAssetPath(image.path)) { m_materialStatus = "Embedded image path is too long"; return; }
            } while (reserved.contains(pathKey(image.path)));
        }
        if (ec) { m_materialStatus = "Cannot inspect " + image.path + ": " + ec.message(); return; }
        reserved.insert(pathKey(image.path));
        // Reuse identical files and give different images their own paths.
        if (image.path != originalPath) { renamed.emplace(originalPath, image.path); }
        if (!reuse) { pending.push_back(&image); }
    }
    std::vector<std::string> materialBytes;
    materialBytes.reserve(extraction.materials.size());
    for (auto& material : extraction.materials) {
        bool changed = false;
        for (auto* path : {&material.baseColorTexture, &material.metallicRoughnessTexture,
                           &material.normalTexture, &material.emissiveTexture, &material.occlusionTexture,
                           &material.transmissionTexture, &material.thicknessTexture, &material.clearcoatTexture,
                           &material.clearcoatRoughnessTexture, &material.clearcoatNormalTexture}) {
            if (const auto found = renamed.find(*path); found != renamed.end()) {
                *path = found->second;
                changed = true;
            }
        }
        const auto& encoded = materialBytes.emplace_back(encodeMaterial(material));
        if (changed && !decodeMaterial(encoded, material, m_materialStatus)) { return; }
    }

    std::size_t imagesWritten = 0;
    for (const auto* image : pending) {
        if (!writeAsset(assetFile(m_project, image->path), image->path, image->bytes, m_materialStatus)) { break; }
        ++imagesWritten;
    }
    std::size_t materialsWritten = 0;
    if (imagesWritten == pending.size()) {
        for (std::size_t i = 0; i < extraction.paths.size(); ++i) {
            const auto& path = extraction.paths[i];
            if (!extraction.overwrite[i] && fileEntryExists(assetFile(m_project, path), ec)) {
                m_materialStatus = "Material already exists: " + path;
                break;
            }
            if (ec) { m_materialStatus = "Cannot inspect " + path + ": " + ec.message(); break; }
            if (!writeAsset(assetFile(m_project, path), path, materialBytes[i], m_materialStatus)) { break; }
            ++materialsWritten;
            m_renderer.invalidateMaterial(path);
        }
    }
    if (materialsWritten == extraction.paths.size()) {
        m_materialStatus = materialsWritten == 1 ? "Extracted " + extraction.paths.front() :
            "Extracted " + std::to_string(materialsWritten) + " materials beside the model.";
        return;
    }

    // Keep images referenced by any materials already published.
    std::unordered_set<std::string_view> savedTextures;
    for (std::size_t i = 0; i < materialsWritten; ++i) {
        const auto& material = extraction.materials[i];
        savedTextures.insert({material.baseColorTexture, material.metallicRoughnessTexture,
            material.normalTexture, material.emissiveTexture, material.occlusionTexture,
            material.transmissionTexture, material.thicknessTexture, material.clearcoatTexture,
            material.clearcoatRoughnessTexture, material.clearcoatNormalTexture});
    }
    while (imagesWritten > 0) {
        const auto& path = pending[--imagesWritten]->path;
        if (savedTextures.contains(path)) { continue; }
        std::filesystem::remove(assetFile(m_project, path), ec);
        if (ec) { m_materialStatus += "\nCannot remove " + path + ": " + ec.message(); }
    }
    if (materialsWritten > 0) {
        m_materialStatus = "Extracted " + std::to_string(materialsWritten) + " of " +
            std::to_string(extraction.paths.size()) + " materials.\n" + m_materialStatus;
    }
}
void AssetImportPanel::drawMeshMaterials() {
    ImGui::Separator();
    if (!m_materialNames.empty()) {
        if (ImGui::BeginCombo("Material", m_materialNames[m_materialIndex].c_str())) {
            for (std::size_t i = 0; i < m_materialNames.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(m_materialNames[i].c_str(), i == m_materialIndex)) { m_materialIndex = i; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Extract Material")) { requestMaterialExtraction(false); }
        if (ImGui::Button("Extract All Materials")) { requestMaterialExtraction(true); }
        if (ImGui::BeginPopupModal("Overwrite Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!m_materialExtraction) {
                ImGui::CloseCurrentPopup();
            } else {
                auto& extraction = *m_materialExtraction;
                if (extraction.paths.size() == 1) {
                    ImGui::TextWrapped("Overwrite %s?", extraction.paths.front().c_str());
                } else {
                    ImGui::TextWrapped("Overwrite the existing material files?");
                    ImGui::BeginChild("Existing materials", ImVec2(440, 120));
                    for (std::size_t i = 0; i < extraction.paths.size(); ++i) {
                        if (extraction.overwrite[i]) { ImGui::TextUnformatted(extraction.paths[i].c_str()); }
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Overwrite")) {
                    saveMaterialExtraction(extraction);
                    m_materialExtraction.reset();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    m_materialExtraction.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    } else if (m_materialStatus.empty()) {
        ImGui::TextDisabled("This model has no materials.");
    }
    if (!m_materialStatus.empty()) { ImGui::TextWrapped("%s", m_materialStatus.c_str()); }
}
void AssetImportPanel::draw(bool readOnly) {
    ImGui::Begin("Inspector");
    ImGui::TextWrapped("%s", m_path.c_str());
    if (isAnimationClipAssetPath(m_path)) { drawAnimationClip(readOnly); ImGui::End(); return; }
    if (isMaterialAssetPath(m_path)) {
        drawMaterial(readOnly);
        ImGui::End();
        return;
    }
    const bool mesh = isMeshAssetPath(m_path);
    const bool gltf = isGltfAssetPath(m_path);
    ImGui::TextUnformatted(mesh ? "Mesh Import Settings" : "Texture Import Settings");
    ImGui::Separator();
    const bool loadingMesh = m_meshInfoPending || (m_meshInfo.valid() && !m_meshInfoObsolete);
    ImGui::BeginDisabled(readOnly || loadingMesh);
    if (mesh) {
        dragFloat("Scale", &m_edited.scale, 0.01f);
        if (gltf) {
            ImGui::Checkbox("Preserve Node Hierarchy", &m_edited.preserveNodeHierarchy);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Create selectable child entities when dragging this asset into the Hierarchy.");
            }
            ImGui::Checkbox("Merge Geometry by Material", &m_edited.mergeGeometry);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Batch compatible opaque and alpha-masked geometry, including selectable child meshes.");
            }
        } else {
            int handedness = m_edited.rightHanded ? 0 : 1;
            if (ImGui::Combo("Source coordinates", &handedness, "Right-handed\0Left-handed\0")) { m_edited.rightHanded = handedness == 0; }
            ImGui::Checkbox("Flip UV V", &m_edited.flipV);
        }
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
    if (ImGui::Button("Apply")) { startImport(); }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) { revert(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(dirty() || !m_valid);
    if (ImGui::Button("Reimport")) { startImport(true); }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (dirty()) { ImGui::TextUnformatted("Unapplied changes"); }
    if (loadingMesh) { ImGui::TextUnformatted("Reading mesh…"); }
    if (!m_info.empty()) { ImGui::TextUnformatted(m_info.c_str()); }
    if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
    if (mesh) {
        ImGui::BeginDisabled(readOnly || loadingMesh);
        drawMeshMaterials();
        ImGui::EndDisabled();
        if (gltf) {
            if (!loadingMesh && ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (m_textureNames.empty()) {
                    ImGui::TextDisabled("No textures.");
                } else {
                    for (const auto& name : m_textureNames) {
                        ImGui::Bullet();
                        ImGui::TextWrapped("%s", name.c_str());
                    }
                }
                ImGui::TreePop();
            }
            ImGui::BeginDisabled(readOnly || loadingMesh);
            drawAnimations();
            ImGui::EndDisabled();
        }
    }
    if (readOnly) { ImGui::TextDisabled("Import settings cannot change during Play."); }
    ImGui::End();
}
void AssetImportPanel::drawPrompt() {
    pollMeshInfo();
    constexpr auto kPopup = "Asset Import";
    if (m_openPrompt) { ImGui::OpenPopup(kPopup); m_openPrompt = false; }
    std::function<void()> action;
    if (ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_import.valid()) {
            ImGui::TextWrapped("%s %s", m_reimport ? "Reimporting" : "Applying changes to", m_path.c_str());
            ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), ImVec2(320, 0), "Working...");
            // Give the dialog a rendered frame before publishing a ready result.
            if (m_importShown && m_import.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                if (finishImport()) {
                    action = std::exchange(m_pending, {});
                    ImGui::CloseCurrentPopup();
                } else if (!m_pending) {
                    ImGui::CloseCurrentPopup();
                }
            }
            m_importShown = true;
        } else {
            ImGui::TextWrapped("Apply changes to %s before continuing?", m_path.c_str());
            if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
            if (ImGui::Button("Apply")) {
                if (isAnimationClipAssetPath(m_path)) {
                    if (saveAnimationClip()) { action = std::exchange(m_pending, {}); ImGui::CloseCurrentPopup(); }
                } else { startImport(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert")) {
                revert();
                action = std::exchange(m_pending, {});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { m_pending = {}; ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }
    if (action) { action(); }
}
}
