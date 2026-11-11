#pragma once

#include "GpuMesh.hpp"
#include "GpuAssetCache.hpp"
#include "SceneDraw.hpp"

#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

class MeshPass {
public:
    MeshPass() = default;
    ~MeshPass();

    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context,
        const Mat4& viewProj,
        const SceneDrawList& drawList) const;

    // Geometry owned by this pass for the given primitive (nullptr if unknown/unready).
    [[nodiscard]] const GpuMesh* geometry(const MeshInstance& instance) const;
    void setAssetReader(AssetReader reader);
    [[nodiscard]] bool reimportAsset(ID3D11Device* device, std::string_view path, const AssetImportSettings* settings,
        const AssetImportWriter& writer, std::string& error) { return m_assets.reimport(device, path, settings, writer, error); }
    [[nodiscard]] std::string assetInfo(std::string_view path) { return m_assets.info(path); }
    void prepareAssets(ID3D11Device* device, const SceneDrawList& drawList);
    [[nodiscard]] const std::vector<std::string>& assetErrors() const { return m_assets.errors(); }
    [[nodiscard]] bool meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum) {
        return m_assets.bounds(path, minimum, maximum);
    }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    GpuAssetCache m_assets;
};

} // namespace sat
