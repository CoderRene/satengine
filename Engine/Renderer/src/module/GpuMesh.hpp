#pragma once

#include "AssetData.hpp"
#include "Math.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sat {

struct GpuMaterial;
struct GpuMeshSection {
    MeshSection range;
    UINT vertexCount = 0;
    const GpuMaterial* material = nullptr; // Owned by the mesh cache.
    float center[3]{}; // Mesh-local center for sorting blended sections.
};

struct GpuMeshNode {
    std::uint32_t id = 0;
    UINT firstSection = 0;
    UINT sectionCount = 0;
    UINT firstIndex = 0;
    UINT indexCount = 0;
    UINT vertexCount = 0;
    Vec3 origin{};
    Vec3 minimum{};
    Vec3 maximum{};
};

// Geometry is baked in asset space. Translate that space to the entity's pivot.
inline Mat4 meshWorld(const Mat4& world, const GpuMeshNode* node) {
    if (!node) { return world; }
    Mat4 result = world;
    const Vec3 position = transformPoint(world, node->origin * -1.0f);
    result.m[12] = position.x;
    result.m[13] = position.y;
    result.m[14] = position.z;
    return result;
}

// Non-owning view of GPU mesh buffers used by mesh / outline passes.
struct GpuMesh {
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* skinBuffer = nullptr;
    std::vector<AnimationBounds> jointBounds;
    std::vector<Mat4> restPalette;
    ID3D11Buffer* indexBuffer = nullptr;
    UINT indexCount = 0;
    UINT vertexCount = 0;
    UINT vertexStride = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
    float boundsCenter[3]{}; // Mesh-local bounding sphere.
    float boundsRadius = 0.0f;
    UINT mainSectionCount = 0;
    bool mergeNodeDraws = false;
    std::vector<GpuMeshSection> sections;
    std::vector<GpuMeshNode> nodes; // Sorted by source node ID.

    [[nodiscard]] const GpuMeshNode* node(std::uint32_t id) const {
        if (id == 0) { return nullptr; }
        const auto found = std::lower_bound(nodes.begin(), nodes.end(), id,
            [](const GpuMeshNode& entry, std::uint32_t value) { return entry.id < value; });
        return found != nodes.end() && found->id == id ? &*found : nullptr;
    }

    [[nodiscard]] bool valid() const {
        return vertexBuffer != nullptr && indexBuffer != nullptr && indexCount > 0 &&
            vertexStride > 0;
    }
};

} // namespace sat
