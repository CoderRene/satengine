#pragma once

#include "GpuMesh.hpp"
#include "SceneDraw.hpp"

#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sat {

class GpuAssetCache;

struct StaticMeshSource {
    const MeshInstance* instance;
    const GpuMesh* mesh;
    const GpuMaterial* material;
    UINT firstIndex;
    UINT indexCount;
    std::size_t drawIndex;
    const GpuMeshNode* node;
};

// Render-thread cache. Batches use a fixed local origin and are rebased per view.
class StaticMeshBatches {
public:
    struct Batch {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertices;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indices;
        GpuMesh mesh;
        MeshInstance instance;
        const GpuMaterial* material = nullptr;
    };

    void update(ID3D11DeviceContext& context, GpuAssetCache& assets, std::uint64_t assetRevision,
        std::span<const StaticMeshSource> sources, DVec3 renderOrigin, std::span<unsigned char> combined);
    [[nodiscard]] const std::vector<Batch>& batches() const { return m_batches; }

private:
    struct SourceKey {
        const GpuMesh* mesh;
        const GpuMaterial* material;
        const GpuMeshNode* node;
        UINT firstIndex;
        UINT indexCount;
        std::array<float, 12> basis;
        DVec3 position;

        bool operator==(const SourceKey&) const = default;
    };

    std::vector<SourceKey> m_keys;
    std::vector<std::size_t> m_combinedSources;
    std::vector<Batch> m_batches;
    std::uint64_t m_assetRevision = 0;
};

} // namespace sat
