#pragma once

#include "ShaderLibrary.hpp"

#include "GpuMesh.hpp"
#include "GpuAssetCache.hpp"
#include "SceneDraw.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

struct GameViewStats;
class GpuViewStats;

class MeshPass {
public:
    MeshPass() = default;
    ~MeshPass();

    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    [[nodiscard]] bool bindSkin(ID3D11DeviceContext* context, EntityId id, const GpuMesh& mesh) const;
    void draw(
        ID3D11DeviceContext* context, ShaderLibrary& shaders,
        const Mat4& view, const Mat4& projection, Vec3 eye,
        const SceneDrawList& drawList, bool hdr = false, DVec3 renderOrigin = {},
        bool prepareAssets = true, GameViewStats* stats = nullptr, ID3D11ShaderResourceView* depth = nullptr,
        GpuViewStats* gpuStats = nullptr, std::uint64_t frame = 0, bool sceneView = false);

    // Geometry owned by this pass for the given primitive (nullptr if unknown/unready).
    [[nodiscard]] const GpuMesh* geometry(const MeshInstance& instance) const;
    [[nodiscard]] const GpuMaterial* material(std::string_view path) const { return m_assets.material(path, false); }
    void invalidateMaterial(std::string_view path) { m_assets.invalidateMaterial(path); ++m_assetRevision; }
    void invalidateAsset(std::string_view path) { m_assets.invalidateAsset(path); ++m_assetRevision; }
    void setAssetReader(AssetReader reader, bool asynchronous = false);
    void refreshAssets(ID3D11Device* device, const std::unordered_set<std::string>& paths) {
        m_assets.refresh(device, paths);
        ++m_assetRevision;
    }
    [[nodiscard]] bool pollAssetUpload(ID3D11Device* device, bool allowReload) {
        if (!m_assets.pollUpload(device, allowReload)) { return false; }
        ++m_assetRevision;
        return true;
    }
    [[nodiscard]] bool hasResidentMesh(std::string_view path) const { return m_assets.mesh(path) != nullptr; }
    void prepareThumbnail(ID3D11Device* device, AssetReader reader, std::string_view path) {
        m_assets.prepareThumbnail(device, std::move(reader), path);
    }
    [[nodiscard]] bool prepareResidentThumbnail(ID3D11Device* device, std::string_view path,
        std::vector<std::string>& dependencies) {
        return m_assets.prepareResidentThumbnail(device, path, dependencies);
    }
    [[nodiscard]] bool reimportAsset(ID3D11Device* device, std::string_view path, const AssetImportSettings* settings,
        const AssetImportWriter& writer, std::string& error) {
        if (!m_assets.reimport(device, path, settings, writer, error)) { return false; }
        ++m_assetRevision;
        return true;
    }
    [[nodiscard]] bool publishAssetImport(ID3D11Device* device, PreparedAssetImport&& prepared,
        const AssetImportWriter& writer, std::string& error) {
        if (!m_assets.publishImport(device, std::move(prepared), writer, error)) { return false; }
        ++m_assetRevision;
        return true;
    }
    [[nodiscard]] std::string assetInfo(std::string_view path) { return m_assets.info(path); }
    [[nodiscard]] std::uint64_t sceneDrawRevision(const Scene& scene);
    void prepareAssets(ID3D11Device* device, const SceneDrawList& drawList);
    [[nodiscard]] const std::vector<std::string>& assetErrors() const { return m_assets.errors(); }
    [[nodiscard]] bool meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum, std::uint32_t meshNode = 0) {
        return m_assets.bounds(path, minimum, maximum, meshNode);
    }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    GpuAssetCache m_assets;
    std::uint64_t m_assetRevision = 0;
};

} // namespace sat
