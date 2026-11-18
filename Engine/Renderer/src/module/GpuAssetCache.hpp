#pragma once

#include "AssetLibrary.hpp"
#include "GpuMesh.hpp"
#include "SceneDraw.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
// Device-thread only. Owns asset GPU resources and decoded CPU cache.
// Reset at project/source changes, before rendering with the new source.
struct GpuMaterial {
    AlphaMode alphaMode = AlphaMode::Opaque;
    std::string shaderPath;
    ID3D11Buffer* constants = nullptr;
    ID3D11ShaderResourceView* views[4]{};
    ID3D11SamplerState* samplers[10]{};
    ID3D11ShaderResourceView* extraViews[6]{}; // Occlusion, transmission, thickness, and three clearcoat maps.
    bool doubleSided = false;
    bool transmissive = false;
    bool volume = false;
};

class GpuAssetCache {
public:
    GpuAssetCache();
    ~GpuAssetCache();
    GpuAssetCache(const GpuAssetCache&) = delete;
    GpuAssetCache& operator=(const GpuAssetCache&) = delete;
    void setReader(AssetReader reader);
    void releaseDecodedAssets();
    void invalidateMaterial(std::string_view path);
    void invalidateAsset(std::string_view path);
    [[nodiscard]] const GpuMaterial* material(std::string_view path, bool fallback = true) const;
    [[nodiscard]] ID3D11ShaderResourceView* environment(std::string_view path) const;
    void prepare(ID3D11Device* device, const SceneDrawList& drawList, ID3D11DeviceContext* context = nullptr);
    [[nodiscard]] bool reimport(ID3D11Device* device, std::string_view path, const AssetImportSettings* settings,
        const AssetImportWriter& writer, std::string& error);
    [[nodiscard]] bool publishImport(ID3D11Device* device, PreparedAssetImport&& prepared,
        const AssetImportWriter& writer, std::string& error);
    [[nodiscard]] std::string info(std::string_view path);
    [[nodiscard]] ID3D11SamplerState* sampler(std::string_view path) const;
    // Borrowed until the next decoded asset access.
    [[nodiscard]] const MeshData* decodedMesh(std::string_view path);
    [[nodiscard]] const GpuMesh* mesh(std::string_view path) const;
    [[nodiscard]] ID3D11ShaderResourceView* texture(std::string_view path) const;
    [[nodiscard]] bool bounds(std::string_view path, Vec3& minimum, Vec3& maximum, std::uint32_t meshNode = 0);
    [[nodiscard]] const std::vector<std::string>& errors() const;
private:
    [[nodiscard]] bool publishPrepared(ID3D11Device* device, PreparedAssetImport& prepared, bool exists,
        const AssetImportWriter& writer, std::string& error);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace sat
