#include "StaticMeshBatches.hpp"

#include "GpuAssetCache.hpp"
#include "ShaderLayout.hpp"

#include <algorithm>
#include <cmath>

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

template <typename Remap>
bool appendGeometry(const StaticMeshSource& source, const MeshData& data, DVec3 origin,
    std::size_t vertexLimit, std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices,
    Remap& remap, std::size_t sourceTag) {
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
    float normalColumns[12];
    writeNormalColumns(normalColumns, local);

    if (remap.size() < data.vertices.size()) { remap.resize(data.vertices.size()); }
    for (UINT i = 0; i < source.indexCount; ++i) {
        const auto index = sourceIndices[firstIndex + i];
        if (index >= data.vertices.size()) { return false; }
        auto& mapped = remap[index];
        if (mapped.sourceTag != sourceTag) {
            if (vertices.size() == vertexLimit) { return false; }
            auto vertex = data.vertices[index];
            const Vec3 position = transformPoint(local, {vertex.x, vertex.y, vertex.z});
            const Vec3 normal = transformDirection(normalColumns, {vertex.nx, vertex.ny, vertex.nz});
            const Vec3 tangent = transformDirection(local.m, {vertex.tx, vertex.ty, vertex.tz});
            vertex.x = position.x; vertex.y = position.y; vertex.z = position.z;
            vertex.nx = normal.x; vertex.ny = normal.y; vertex.nz = normal.z;
            vertex.tx = tangent.x; vertex.ty = tangent.y; vertex.tz = tangent.z;
            vertex.tw *= normalColumns[3];
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
    const auto position = [](const MeshVertex& vertex) { return Vec3{vertex.x, vertex.y, vertex.z}; };
    Vec3 minimum = position(vertices.front()), maximum = minimum;
    for (const auto& vertex : vertices) {
        minimum = {std::min(minimum.x, vertex.x), std::min(minimum.y, vertex.y), std::min(minimum.z, vertex.z)};
        maximum = {std::max(maximum.x, vertex.x), std::max(maximum.y, vertex.y), std::max(maximum.z, vertex.z)};
    }
    const Vec3 center = (minimum + maximum) * 0.5f;
    batch.mesh.boundsCenter[0] = center.x;
    batch.mesh.boundsCenter[1] = center.y;
    batch.mesh.boundsCenter[2] = center.z;
    batch.mesh.boundsRadius = length(maximum - center);
    return true;
}

} // namespace

bool StaticMeshBatches::update(ID3D11DeviceContext& context, GpuAssetCache& assets,
    std::span<const StaticMeshSource> sources, std::span<const unsigned> dirtySources, bool budgetAvailable,
    std::span<unsigned char> combined) {
    if (dirtySources.empty() && (!m_pending || !budgetAvailable)) { return false; }
    m_locations.resize(sources.size());
    bool changed = false;
    const auto retire = [&](Cell& cell) {
        if (cell.batch.mesh.valid()) {
            for (const auto index : cell.sources) { combined[sources[index].drawIndex] = 0; }
            cell.batch = {};
            changed = true;
        }
        cell.dirty = true;
    };
    for (const auto index : dirtySources) {
        const auto& source = sources[index];
        const auto position = source.instance->worldPosition;
        auto& previous = m_locations[index];
        const bool finite = std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
        const auto origin = finite ? cellOrigin(position) : DVec3{};
        const CellKey key{reinterpret_cast<std::uintptr_t>(source.material), origin.x, origin.y, origin.z};
        if (previous) {
            auto current = m_cells.find(*previous);
            retire(current->second);
            if (finite && *previous == key) { continue; }
            current->second.sources.erase(index);
            if (current->second.sources.empty()) { m_cells.erase(current); }
            previous.reset();
        }
        if (finite) {
            auto& cell = m_cells[key];
            retire(cell);
            cell.material = source.material;
            cell.origin = origin;
            cell.sources.insert(index);
            previous = key;
        }
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    std::size_t remainingVertices = budgetAvailable ? kVertexBudget : 0;
    std::size_t remainingIndices = budgetAvailable ? kIndexBudget : 0;
    std::size_t tag = 0;
    m_remap.clear();
    m_pending = false;
    for (auto& entry : m_cells) {
        auto& cell = entry.second;
        if (!cell.dirty) { continue; }
        const auto& first = sources[*cell.sources.begin()];
        bool different = false;
        std::size_t indexCount = 0;
        for (const auto index : cell.sources) {
            const auto& source = sources[index];
            different |= source.mesh != first.mesh || source.firstIndex != first.firstIndex ||
                source.indexCount != first.indexCount;
            indexCount += source.indexCount;
            if (indexCount > kIndexBudget) { break; }
        }
        if (!different || indexCount > kIndexBudget) {
            cell.dirty = false; // Single-geometry and oversized cells stay instanced.
            continue;
        }
        if (indexCount > remainingIndices || remainingVertices == 0) {
            m_pending = true;
            continue;
        }
        if (!device) { context.GetDevice(device.GetAddressOf()); }
        m_vertices.clear();
        m_indices.clear();
        m_indices.reserve(indexCount);
        bool ready = true;
        bool budgetLimited = false;
        for (const auto index : cell.sources) {
            const auto& source = sources[index];
            const auto* data = assets.decodedMesh(source.instance->meshPath);
            if (!data || !appendGeometry(source, *data, cell.origin, remainingVertices,
                    m_vertices, m_indices, m_remap, ++tag)) {
                ready = false;
                budgetLimited = remainingVertices < kVertexBudget && m_vertices.size() == remainingVertices;
                break;
            }
        }
        if (ready && upload(*device.Get(), m_vertices, m_indices, cell.batch)) {
            cell.batch.material = cell.material;
            cell.batch.instance.worldPosition = cell.origin;
            for (const auto index : cell.sources) { combined[sources[index].drawIndex] = 1; }
            changed = true;
        } else {
            cell.batch = {};
        }
        // Failed attempts also consume the work budget.
        remainingVertices -= m_vertices.size();
        remainingIndices -= indexCount;
        cell.dirty = budgetLimited;
        m_pending |= budgetLimited;
    }
    if (changed) {
        m_batches.clear();
        for (const auto& entry : m_cells) {
            const auto& batch = entry.second.batch;
            if (batch.mesh.valid()) { m_batches.push_back(&batch); }
        }
    }
    return changed;
}

} // namespace sat
