#pragma once

#include "AssetLibrary.hpp"

#include <cstddef>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
class Project;
class Renderer;
// Window-thread only. Project/renderer references must outlive this panel.
// Owns the single asset selection, saved settings, draft and deferred UI action.
class AssetImportPanel {
public:
    AssetImportPanel(Project& project, Renderer& renderer);
    void select(std::string path);
    void clear();
    [[nodiscard]] bool active() const { return !m_path.empty(); }
    [[nodiscard]] bool dirty() const { return m_edited != m_saved || m_material != m_savedMaterial; }
    [[nodiscard]] const std::string& path() const { return m_path; }
    void request(std::function<void()> action);
    void draw(bool readOnly);
    [[nodiscard]] std::string updateMaterialReferences(std::string_view oldPath, std::string_view newPath);
    void drawPrompt();
private:
    struct ImportResult {
        PreparedAssetImport asset;
        std::optional<MaterialData> material;
        std::vector<std::string> gltfMaterials;
        std::string gltfStatus;
        std::string error;
    };
    struct MeshInfo {
        AssetImportSettings settings;
        std::string info;
        std::string error;
        std::vector<std::string> materials;
        std::string materialError;
        bool validSettings = false;
    };
    void pollMeshInfo();
    void startImport(bool reimport = false);
    [[nodiscard]] bool finishImport();
    void revert();
    void drawMaterial(bool readOnly);
    struct GltfExtraction {
        std::vector<MaterialData> materials;
        std::vector<std::string> paths;
        std::vector<GltfImage> images;
        std::vector<bool> overwrite;
    };
    void drawGltfMaterials();
    void requestGltfExtraction(bool all);
    void saveGltfExtraction(GltfExtraction& extraction);
    Project& m_project;
    Renderer& m_renderer;
    std::string m_path;
    std::string m_error;
    std::string m_info;
    MaterialData m_material;
    MaterialData m_savedMaterial;
    std::vector<std::string> m_texturePaths;
    std::vector<std::string> m_shaderPaths;
    std::vector<std::string> m_gltfMaterials;
    std::string m_gltfStatus;
    std::size_t m_gltfMaterialIndex = 0;
    std::optional<GltfExtraction> m_gltfExtraction;
    AssetImportSettings m_saved;
    AssetImportSettings m_edited;
    bool m_valid = true;
    bool m_openPrompt = false;
    std::function<void()> m_pending;
    bool m_reimport = false;
    bool m_importShown = false;
    std::future<ImportResult> m_import;
    bool m_meshInfoPending = false;
    bool m_meshInfoObsolete = false;
    std::future<MeshInfo> m_meshInfo;
};
}
