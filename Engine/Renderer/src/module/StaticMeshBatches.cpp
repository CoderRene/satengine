#include "StaticMeshBatches.hpp"

#include "GpuAssetCache.hpp"
#include "ShaderLayout.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <tuple>
#include <utility>

namespace sat {
namespace {

constexpr double kCellSize = 256.0;
constexpr std::size_t kVertexBudget = 128 * 1024;
constexpr std::size_t kIndexBudget = 384 * 1024;

DVec3 cellOrigin(DVec3 position) {
    return {std::round(position.x / kCellSize) * kCellSize,
        std::round(position.y / kCellSize) * kCellSize, std::round(position.z / kCellSize) * kCellSize};
}

Vec3 transformDirection(const float* matrix, Vec3 v) {
    return {matrix[0] * v.x + matrix[4] * v.y + matrix[8] * v.z,
        matrix[1] * v.x + matrix[5] * v.y + matrix[9] * v.z,
        matrix[2] * v.x + matrix[6] * v.y + matrix[10] * v.z};
}

struct RemappedVertex {
    std::size_t sourceTag = 0;
    std::uint32_t index = 0;
};

bool appendGeometry(const StaticMeshSource& source, const MeshData& data, DVec3 origin,
    std::size_t vertexLimit, std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices,
    std::vector<RemappedVertex>& remap, std::size_t sourceTag) {
    // Reordered meshes append the original node indices after the main index range.
    const bool useNodeIndices = source.node && !data.nodeIndices.empty();
    const auto& sourceIndices = useNodeIndices ? data.nodeIndices : data.indices;
    const auto offsetInBuffer = useNodeIndices ? data.indices.size() : 0;
    if (source.firstIndex < offsetInBuffer) { return false; }
    const auto firstIndex = source.firstIndex - offsetInBuffer;
    if (firstIndex > sourceIndices.size() || source.indexCount > sourceIndices.size() - firstIndex) {
        return false;
    }
    const Vec3 offset = toVec3(source.instance->worldPosition - origin);
    Mat4 local = source.instance->world;
    local.m[12] = offset.x;
    local.m[13] = offset.y;
    local.m[14] = offset.z;
    local = meshWorld(local, source.node);
    MeshInstanceData object;
    writeMeshInstance(object, local);

    if (remap.size() < data.vertices.size()) { remap.resize(data.vertices.size()); }
    for (UINT i = 0; i < source.indexCount; ++i) {
        const auto index = sourceIndices[firstIndex + i];
        if (index >= data.vertices.size()) { return false; }
        auto& mapped = remap[index];
        if (mapped.sourceTag != sourceTag) {
            if (vertices.size() == vertexLimit) { return false; }
            auto vertex = data.vertices[index];
            const Vec3 position = transformPoint(local, {vertex.x, vertex.y, vertex.z});
            const Vec3 normal = transformDirection(object.normal, {vertex.nx, vertex.ny, vertex.nz});
            const Vec3 tangent = transformDirection(object.world, {vertex.tx, vertex.ty, vertex.tz});
            vertex.x = position.x; vertex.y = position.y; vertex.z = position.z;
            vertex.nx = normal.x; vertex.ny = normal.y; vertex.nz = normal.z;
            vertex.tx = tangent.x; vertex.ty = tangent.y; vertex.tz = tangent.z;
            vertex.tw *= object.normal[3];
            mapped = {sourceTag, static_cast<std::uint32_t>(vertices.size())};
            vertices.push_back(vertex);
        }
        indices.push_back(mapped.index);
    }
    return true;
}

bool upload(ID3D11Device& device, const std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices, StaticMeshBatches::Batch& batch) {
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(MeshVertex));
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    if (FAILED(device.CreateBuffer(&desc, &data, batch.vertices.GetAddressOf()))) { return false; }
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    data.pSysMem = indices.data();
    if (FAILED(device.CreateBuffer(&desc, &data, batch.indices.GetAddressOf()))) { return false; }
    batch.mesh.vertexBuffer = batch.vertices.Get();
    batch.mesh.indexBuffer = batch.indices.Get();
    batch.mesh.vertexStride = sizeof(MeshVertex);
    batch.mesh.indexFormat = DXGI_FORMAT_R32_UINT;
    batch.mesh.vertexCount = static_cast<UINT>(vertices.size());
    batch.mesh.indexCount = static_cast<UINT>(indices.size());
    return true;
}

} // namespace

void StaticMeshBatches::update(ID3D11DeviceContext& context, GpuAssetCache& assets, std::uint64_t assetRevision,
    std::span<const StaticMeshSource> sources, DVec3 renderOrigin, std::span<unsigned char> combined) {
    const auto key = [](const StaticMeshSource& source) {
        SourceKey result{source.mesh, source.material, source.node, source.firstIndex, source.indexCount,
            {}, source.instance->worldPosition};
        std::copy_n(source.instance->world.m, result.basis.size(), result.basis.begin());
        return result;
    };
    if (assetRevision != m_assetRevision || sources.size() != m_keys.size() ||
        !std::equal(sources.begin(), sources.end(), m_keys.begin(),
            [&](const auto& source, const auto& stored) { return key(source) == stored; })) {
        m_assetRevision = assetRevision;
        m_keys.clear();
        m_combinedSources.clear();
        m_batches.clear();
        m_keys.reserve(sources.size());
        for (const auto& source : sources) { m_keys.push_back(key(source)); }

        std::vector<std::size_t> order(sources.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        const auto cell = [&](std::size_t i) {
            const auto origin = cellOrigin(m_keys[i].position);
            return std::tuple{origin.x, origin.y, origin.z};
        };
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (sources[a].material != sources[b].material) {
                return std::less<>{}(sources[a].material, sources[b].material);
            }
            return std::tuple{cell(a), a} < std::tuple{cell(b), b};
        });

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        if (order.size() > 1) { context.GetDevice(device.GetAddressOf()); }
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<RemappedVertex> remap;
        std::size_t remainingVertices = kVertexBudget;
        std::size_t remainingIndices = kIndexBudget;
        for (std::size_t first = 0; first < order.size();) {
            const auto& source = sources[order[first]];
            std::size_t last = first + 1;
            std::size_t indexCount = source.indexCount;
            bool differentGeometry = false;
            while (last < order.size() && source.material == sources[order[last]].material &&
                cell(order[first]) == cell(order[last])) {
                const auto& next = sources[order[last]];
                differentGeometry |= next.mesh != source.mesh || next.firstIndex != source.firstIndex ||
                    next.indexCount != source.indexCount;
                indexCount += next.indexCount;
                ++last;
            }
            // Keep groups with a single geometry range on the instancing path.
            if (differentGeometry && indexCount <= remainingIndices && remainingVertices != 0) {
                Batch batch;
                batch.material = source.material;
                batch.instance.worldPosition = cellOrigin(source.instance->worldPosition);
                vertices.clear();
                indices.clear();
                indices.reserve(indexCount);
                bool ready = true;
                for (std::size_t i = first; i < last; ++i) {
                    const auto& entry = sources[order[i]];
                    const auto* data = assets.decodedMesh(entry.instance->meshPath);
                    if (!data || !appendGeometry(entry, *data, batch.instance.worldPosition,
                            remainingVertices, vertices, indices, remap, i + 1)) {
                        ready = false;
                        break;
                    }
                }
                if (ready && upload(*device.Get(), vertices, indices, batch)) {
                    remainingVertices -= vertices.size();
                    remainingIndices -= indices.size();
                    m_batches.push_back(std::move(batch));
                    const auto group = std::span(order).subspan(first, last - first);
                    m_combinedSources.insert(m_combinedSources.end(), group.begin(), group.end());
                }
            }
            first = last;
        }
    }
    for (const auto index : m_combinedSources) { combined[sources[index].drawIndex] = 1; }
    for (auto& batch : m_batches) {
        batch.instance.world = translationMatrix(toVec3(batch.instance.worldPosition - renderOrigin));
    }
}

} // namespace sat
