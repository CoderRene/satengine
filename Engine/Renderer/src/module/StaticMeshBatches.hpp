#pragma once

#include "GpuMesh.hpp"
#include "SceneDraw.hpp"

#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <map>
#include <optional>
#include <set>
#include <tuple>
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

    // Returns true when the published merged geometry or membership changed.
    [[nodiscard]] bool update(ID3D11DeviceContext& context, GpuAssetCache& assets,
        std::span<const StaticMeshSource> sources, std::span<const unsigned> dirtySources, bool budgetAvailable,
        std::span<unsigned char> combined);
    [[nodiscard]] const std::vector<const Batch*>& batches() const { return m_batches; }
    [[nodiscard]] bool pending() const { return m_pending; }

private:
    using CellKey = std::tuple<std::uintptr_t, double, double, double>;
    struct Cell {
        const GpuMaterial* material = nullptr;
        DVec3 origin;
        std::set<std::size_t> sources;
        Batch batch;
        bool dirty = true;
    };
    struct RemappedVertex {
        std::size_t sourceTag = 0;
        std::uint32_t index = 0;
    };
    std::vector<MeshVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    std::vector<RemappedVertex> m_remap;
    std::map<CellKey, Cell> m_cells; // Stable batch addresses until the cell is retired.
    std::vector<std::optional<CellKey>> m_locations;
    std::vector<const Batch*> m_batches;
    bool m_pending = false;
};

} // namespace sat
