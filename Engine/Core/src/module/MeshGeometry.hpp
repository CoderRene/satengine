#pragma once

#include "AssetData.hpp"

namespace sat {
namespace meshGeometry {
inline constexpr std::size_t kMaxVertices = 1000000;
inline constexpr std::size_t kMaxIndices = 3000000;

Vec3 unitVector(const Vec3& vector);
void accumulateTangent(MeshData& mesh, std::vector<Vec3>& bitangents,
    std::uint32_t ia, std::uint32_t ib, std::uint32_t ic);
void finalizeMesh(MeshData& mesh, const std::vector<Vec3>& bitangents);
}
}
