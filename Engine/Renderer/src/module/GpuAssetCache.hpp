#pragma once

#include "AssetLibrary.hpp"
#include "GpuMesh.hpp"
#include "SceneDraw.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sat {
// Device-thread only. Owns asset GPU resources and decoded CPU cache.
// Reset at project/source changes, before rendering with the new source.
class GpuAssetCache {
public:
    GpuAssetCache();
    ~GpuAssetCache();
    GpuAssetCache(const GpuAssetCache&) = delete;
    GpuAssetCache& operator=(const GpuAssetCache&) = delete;
    void setReader(AssetReader reader);
    void prepare(ID3D11Device* device, const SceneDrawList& drawList);
    [[nodiscard]] bool reimport(ID3D11Device* device, std::string_view path, const AssetImportSettings* settings,
        const AssetImportWriter& writer, std::string& error);
    [[nodiscard]] std::string info(std::string_view path);
    [[nodiscard]] ID3D11SamplerState* sampler(std::string_view path) const;
    [[nodiscard]] const GpuMesh* mesh(std::string_view path) const;
    [[nodiscard]] ID3D11ShaderResourceView* texture(std::string_view path) const;
    [[nodiscard]] bool bounds(std::string_view path, Vec3& minimum, Vec3& maximum);
    [[nodiscard]] const std::vector<std::string>& errors() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace sat
