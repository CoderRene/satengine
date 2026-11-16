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

class MeshPass {
public:
    MeshPass() = default;
    ~MeshPass();

    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context, ShaderProgram program,
        const Mat4& view, const Mat4& projection, Vec3 eye,
        const SceneDrawList& drawList, bool hdr = false, ID3D11PixelShader* shadowPixel = nullptr,
        ShaderLibrary* shaders = nullptr, DVec3 renderOrigin = {}, bool loadProjectShaders = true,
        GameViewStats* stats = nullptr);

    // Geometry owned by this pass for the given primitive (nullptr if unknown/unready).
    [[nodiscard]] const GpuMesh* geometry(const MeshInstance& instance) const;
    [[nodiscard]] const GpuMaterial* material(std::string_view path) const { return m_assets.material(path, false); }
    void invalidateMaterial(std::string_view path) { m_assets.invalidateMaterial(path); ++m_assetRevision; }
    void invalidateAsset(std::string_view path) { m_assets.invalidateAsset(path); ++m_assetRevision; }
    void setAssetReader(AssetReader reader);
    void releaseDecodedAssets() { m_assets.releaseDecodedAssets(); }
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
