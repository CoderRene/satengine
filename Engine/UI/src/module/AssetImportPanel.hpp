#pragma once

#include "AssetLibrary.hpp"
#include "AnimationAsset.hpp"
#include "AnimationSystem.hpp"

#include <cstddef>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

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
    [[nodiscard]] bool dirty() const { return m_edited != m_saved || m_material != m_savedMaterial || m_clipDirty; }
    [[nodiscard]] const std::string& path() const { return m_path; }
    void request(std::function<void()> action);
    void draw(bool readOnly);
    [[nodiscard]] std::string updateMaterialReferences(std::string_view oldPath, std::string_view newPath, bool directory);
    void drawPrompt();
private:
    struct ImportResult {
        PreparedAssetImport asset;
        std::optional<MaterialData> material;
        std::vector<std::string> materialNames;
        std::vector<std::string> textureNames;
        std::string materialStatus;
        std::string error;
    };
    struct MeshInfo {
        AssetImportSettings settings;
        std::string info;
        std::string error;
        std::vector<std::string> materials;
        std::vector<std::string> textureNames;
        std::string materialError;
        bool validSettings = false;
    };
    void pollMeshInfo();
    void startImport(bool reimport = false);
    [[nodiscard]] bool finishImport();
    void revert();
    void drawMaterial(bool readOnly);
    struct MaterialExtraction {
        std::vector<MaterialData> materials;
        std::vector<std::string> paths;
        std::vector<GltfImage> images;
        std::vector<bool> overwrite;
    };
    void drawMeshMaterials();
    void drawAnimations();
    void drawAnimationClip(bool readOnly);
    bool saveAnimationClip();
    void extractAnimations(bool all);
    void saveAnimationExtraction();
    Scene m_previewScene;
    AnimationSystem m_previewAnimation{m_previewScene, AnimationSystem::Mode::Preview};
    EntityId m_previewEntity = kInvalidEntityId;
    AnimationData m_animations;
    AnimationClipAsset m_animationClip;
    bool m_animationsLoaded = false;
    bool m_clipDirty = false;
    int m_animationIndex = 0;
    std::vector<std::pair<std::string, std::string>> m_animationExtraction;
    void requestMaterialExtraction(bool all);
    void saveMaterialExtraction(MaterialExtraction& extraction);
    Project& m_project;
    Renderer& m_renderer;
    std::string m_path;
    std::string m_error;
    std::string m_info;
    MaterialData m_material;
    MaterialData m_savedMaterial;
    std::vector<std::string> m_texturePaths;
    std::vector<std::string> m_shaderPaths;
    std::vector<std::string> m_materialNames;
    std::vector<std::string> m_textureNames;
    std::string m_materialStatus;
    std::size_t m_materialIndex = 0;
    std::optional<MaterialExtraction> m_materialExtraction;
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
