#include "GpuScene.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <iostream>
#include <tuple>
#include <utility>

namespace sat {
namespace {

constexpr unsigned kThreads = 64;
constexpr unsigned kDispatchLimit = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION * kThreads;

DVec3 translationOf(const Mat4& world) {
    return {world.m[12], world.m[13], world.m[14]};
}

GpuInstance encodeInstance(Mat4 basis, DVec3 position, Vec3 localCenter, float localRadius,
    unsigned palette, Vec3 thickness) {
    GpuInstance result{};
    if (std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
        splitOrigin(position, result.translationHigh, result.translationLow);
    } else {
        localRadius = -1.0f;
    }
    std::memcpy(result.basis, basis.m, sizeof(result.basis));
    float normal[12];
    writeNormalColumns(normal, basis);
    std::memcpy(result.normal, normal, sizeof(result.normal));
    result.paletteOffset = palette;

    const auto center = transformPoint(basis, localCenter);
    const Vec3 x{basis.m[0], basis.m[1], basis.m[2]};
    const Vec3 y{basis.m[4], basis.m[5], basis.m[6]};
    const Vec3 z{basis.m[8], basis.m[9], basis.m[10]};
    // Maximum row sum of the Gram matrix bounds stretch, including imported shear.
    const float xy = std::abs(dot(x, y)), xz = std::abs(dot(x, z)), yz = std::abs(dot(y, z));
    const float scale = std::sqrt(std::max({dot(x, x) + xy + xz, dot(y, y) + xy + yz, dot(z, z) + xz + yz}));
    result.bounds[0] = center.x;
    result.bounds[1] = center.y;
    result.bounds[2] = center.z;
    const float radius = localRadius * scale;
    result.bounds[3] = localRadius >= 0 && std::isfinite(radius) &&
        std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z) ? radius : -1.0f;
    result.thicknessScale[0] = thickness.x * length(x);
    result.thicknessScale[1] = thickness.y * length(y);
    result.thicknessScale[2] = thickness.z * length(z);
    return result;
}

bool sameTransform(const Transform& a, const Transform& b) {
    return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
}

unsigned passesFor(const GpuMaterial& material) {
    if (material.alphaMode == AlphaMode::Blend) { return meshQueueMask(MeshQueue::Transparent); }
    if (material.transmissive) { return meshQueueMask(MeshQueue::Transmission); }
    return meshQueueMask(MeshQueue::Opaque) | meshQueueMask(MeshQueue::Shadow);
}

auto batchKey(const GpuScene::DrawRange& draw) {
    return std::tuple{draw.passes, draw.mesh->skinVertices != nullptr, draw.shaderKey,
        reinterpret_cast<std::uintptr_t>(draw.material), reinterpret_cast<std::uintptr_t>(draw.mesh->vertexBuffer),
        reinterpret_cast<std::uintptr_t>(draw.mesh->indexBuffer), reinterpret_cast<std::uintptr_t>(draw.mesh),
        draw.firstIndex, draw.indexCount};
}

} // namespace

void splitOrigin(DVec3 origin, float* high, float* low) {
    const double values[]{origin.x, origin.y, origin.z};
    for (unsigned i = 0; i < 3; ++i) {
        high[i] = static_cast<float>(values[i]);
        low[i] = static_cast<float>(values[i] - static_cast<double>(high[i]));
    }
}

bool GpuScene::Structured::ensure(ID3D11Device& device, std::size_t count, UINT stride, bool writable) {
    if (capacity >= count) { return true; }
    if (count > std::numeric_limits<UINT>::max() / stride) { return false; }
    const auto size = std::bit_ceil(count);
    if (size > std::numeric_limits<UINT>::max() / stride) { return false; }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(size * stride);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0);
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = stride;
    D3D11_SHADER_RESOURCE_VIEW_DESC input{};
    input.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    input.Buffer.NumElements = static_cast<UINT>(size);
    D3D11_UNORDERED_ACCESS_VIEW_DESC output{};
    output.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    output.Buffer.NumElements = input.Buffer.NumElements;
    Structured candidate;
    if (FAILED(device.CreateBuffer(&desc, nullptr, candidate.buffer.GetAddressOf())) ||
        FAILED(device.CreateShaderResourceView(candidate.buffer.Get(), &input, candidate.view.GetAddressOf())) ||
        (writable && FAILED(device.CreateUnorderedAccessView(candidate.buffer.Get(), &output, candidate.output.GetAddressOf())))) {
        return false;
    }
    candidate.capacity = size;
    *this = std::move(candidate);
    return true;
}

void GpuScene::Structured::upload(ID3D11DeviceContext& context, const void* data, UINT bytes, UINT offset) const {
    const D3D11_BOX box{offset, 0, 0, offset + bytes, 1, 1};
    context.UpdateSubresource(buffer.Get(), 0, &box, data, 0, 0);
}

void GpuScene::rebuild(const GpuAssetCache& assets, const GpuMesh& cube, const SceneDrawList& list) {
    m_objects.clear();
    m_objectIndices.clear();
    m_ranges.clear();
    m_skinnedObjects.clear();
    m_thicknessScales.clear();
    m_staticSources.clear();
    m_combined.clear();
    m_staticBatches = {};
    m_skinLayoutDirty = m_skinBindingsDirty = true;
    m_skinnedVertexCount = 0;
    m_objects.reserve(list.scene ? m_sources.size() : static_cast<std::size_t>(list.meshCount));
    std::unordered_map<const GpuMaterial*, std::uint64_t> shaderKeys;
    const bool prepareStatic = list.scene || list.meshCount > 1;
    const auto add = [&](MeshInstance instance, std::size_t sourceIndex, EntityId id) {
        const auto* mesh = instance.meshPath.empty() ? &cube : assets.mesh(instance.meshPath);
        if (!mesh || !mesh->valid()) { return; }
        const auto* node = mesh->node(instance.meshNode);
        if (instance.meshNode != 0 && !node) { return; }
        const unsigned object = static_cast<unsigned>(m_objects.size());
        auto& stored = m_objects.emplace_back(Object{{}, instance, mesh, node, sourceIndex,
            static_cast<unsigned>(m_thicknessScales.size())});
        stored.firstStaticSource = static_cast<unsigned>(m_staticSources.size());
        if (mesh->skinVertices) {
            stored.skinnedVertexBase = static_cast<unsigned>(m_skinnedVertexCount);
            m_skinnedVertexCount += mesh->vertexCount;
            m_skinnedObjects.push_back(object);
        }
        if (id != kInvalidEntityId) { m_objectIndices.emplace(id, object); }

        const auto* material = assets.material(instance.materialPath);
        const auto visit = [&](const GpuMaterial* sectionMaterial, UINT first, UINT count, UINT vertices,
            Vec3 center, Vec3 thickness) {
            if (!sectionMaterial || !count) { return; }
            const auto [key, inserted] = shaderKeys.try_emplace(sectionMaterial);
            if (inserted) { key->second = std::hash<std::string_view>{}(sectionMaterial->shaderPath); }
            const auto passes = passesFor(*sectionMaterial);
            const Vec3 scale = sectionMaterial->volume ? thickness : Vec3{1, 1, 1};
            const auto begin = m_thicknessScales.begin() + stored.firstInstance;
            unsigned gpuIndex = static_cast<unsigned>(
                std::find(begin, m_thicknessScales.end(), scale) - m_thicknessScales.begin());
            if (gpuIndex == m_thicknessScales.size()) {
                m_thicknessScales.push_back(scale);
                ++stored.instanceCount;
            }
            const auto rangeIndex = m_ranges.size();
            m_ranges.push_back({{mesh, sectionMaterial, first, count, vertices, passes, key->second},
                gpuIndex, center});
            if (prepareStatic && (passes & meshQueueMask(MeshQueue::Opaque)) != 0 && !mesh->skinVertices &&
                !instance.meshPath.empty() && (node ? mesh->mergeNodeDraws : instance.combineStatic)) {
                ++stored.staticSourceCount;
                m_staticSources.push_back({&stored.meshInstance, mesh, sectionMaterial, first, count, rangeIndex, node});
            }
        };

        const bool overrideMaterial = !instance.materialPath.empty();
        if (mesh->mainSectionCount == 0 || (overrideMaterial && (!material || !material->volume))) {
            const auto& center = mesh->boundsCenter;
            visit(material, node ? node->firstIndex : 0, node ? node->indexCount : mesh->indexCount,
                node ? node->vertexCount : mesh->vertexCount,
                node ? (node->minimum + node->maximum) * 0.5f : Vec3{center[0], center[1], center[2]}, {1, 1, 1});
            return;
        }
        const auto sections = std::span(mesh->sections).subspan(node ? node->firstSection : 0,
            node ? node->sectionCount : mesh->mainSectionCount);
        for (const auto& section : sections) {
            const auto& range = section.range;
            const auto& center = section.center;
            visit(!overrideMaterial && section.material ? section.material : material,
                range.firstIndex, range.indexCount, section.vertexCount, {center[0], center[1], center[2]},
                range.thicknessScale);
        }
    };
    if (list.scene) {
        for (std::size_t i = 0; i < m_sources.size(); ++i) {
            const auto& source = m_sources[i];
            MeshInstance instance{};
            instance.meshPath = source.meshPath;
            instance.materialPath = source.materialPath;
            instance.meshNode = source.meshNode;
            instance.combineStatic = source.combineStatic;
            add(instance, i, source.id);
        }
    } else {
        for (int i = 0; i < list.meshCount; ++i) { add(list.meshes[i], 0, kInvalidEntityId); }
    }

    m_instances.resize(m_thicknessScales.size());
    m_allDirty = m_tableDirty = true;
    m_shaderRevision = ~std::uint64_t{};
    m_combined.resize(m_staticSources.empty() ? 0 : m_ranges.size());
    m_sortedRanges.clear();
    m_sortedRanges.reserve(m_ranges.size());
    m_transparent.clear();
    for (unsigned i = 0; i < m_ranges.size(); ++i) {
        const auto& item = m_ranges[i];
        if (item.range.passes == meshQueueMask(MeshQueue::Transparent)) {
            m_transparent.push_back(item);
        } else {
            m_sortedRanges.push_back(i);
        }
    }
    std::stable_sort(m_sortedRanges.begin(), m_sortedRanges.end(), [&](unsigned a, unsigned b) {
        return batchKey(m_ranges[a].range) < batchKey(m_ranges[b].range);
    });
}

void GpuScene::buildDrawTable() {
    std::vector<DrawItem> merged;
    const auto& staticBatches = m_staticBatches.batches();
    if (!staticBatches.empty()) {
        merged.reserve(staticBatches.size());
        for (unsigned i = 0; i < staticBatches.size(); ++i) {
            const auto& batch = *staticBatches[i];
            merged.push_back({{&batch.mesh, batch.material, 0, batch.mesh.indexCount, batch.mesh.vertexCount,
                passesFor(*batch.material), std::hash<std::string_view>{}(batch.material->shaderPath)},
                static_cast<unsigned>(m_thicknessScales.size()) + i, {}});
        }
        std::sort(merged.begin(), merged.end(), [](const auto& a, const auto& b) {
            return batchKey(a.range) < batchKey(b.range);
        });
    }
    m_batches.clear();
    m_templates.clear();
    m_items.clear();
    m_opaqueItems = m_transmissionItems = {};
    const auto append = [&](const DrawItem& item) {
        const auto& draw = item.range;
        if (m_batches.empty() || batchKey(draw) != batchKey(m_batches.back())) {
            m_batches.push_back(draw);
            m_templates.push_back({draw.indexCount, 0, draw.mesh->firstIndex + draw.firstIndex,
                draw.mesh->baseVertex, static_cast<unsigned>(m_items.size())});
        }
        auto& items = draw.passes == meshQueueMask(MeshQueue::Transmission) ? m_transmissionItems : m_opaqueItems;
        if (items.count++ == 0) { items.start = static_cast<UINT>(m_items.size()); }
        m_items.push_back({item.instance, static_cast<unsigned>(m_batches.size() - 1), m_templates.back().firstInstance,
            draw.passes});
    };
    auto cell = merged.begin();
    for (const auto index : m_sortedRanges) {
        if (!m_combined.empty() && m_combined[index]) { continue; }
        const auto& item = m_ranges[index];
        while (cell != merged.end() && batchKey(cell->range) < batchKey(item.range)) { append(*cell++); }
        append(item);
    }
    while (cell != merged.end()) { append(*cell++); }
    m_tableDirty = true;
}

bool GpuScene::uploadTable(ID3D11DeviceContext& context, ID3D11Device& device) {
    if (!m_tableDirty) { return true; }
    if (!m_items.empty()) {
        if (m_items.size() > kDispatchLimit || !m_itemBuffer.ensure(device, m_items.size(), sizeof(GpuDrawItem))) {
            return false;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(m_templates.size() * sizeof(GpuBatch));
        desc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = m_templates.data();
        if (FAILED(device.CreateBuffer(&desc, &initial, m_args.ReleaseAndGetAddressOf()))) { return false; }
        m_itemBuffer.upload(context, m_items.data(), static_cast<UINT>(m_items.size() * sizeof(GpuDrawItem)));
    }
    m_tableDirty = false;
    return true;
}

bool GpuScene::uploadInstances(ID3D11DeviceContext& context, ID3D11Device& device, ShaderLibrary& shaders) {
    if (m_instances.empty()) { return true; }
    m_allDirty |= m_instances.size() > m_instanceBuffer.capacity;
    if (!m_instanceBuffer.ensure(device, m_instances.size(), sizeof(GpuInstance), true)) { return false; }
    const auto uploadFull = [&] {
        m_instanceBuffer.upload(context, m_instances.data(), static_cast<UINT>(m_instances.size() * sizeof(GpuInstance)));
        return true;
    };
    if (m_allDirty) { return uploadFull(); }
    if (m_dirtyInstances.empty()) { return true; }
    if (m_scatterUnavailable || m_dirtyInstances.size() > kDispatchLimit ||
        m_dirtyInstances.size() > m_instances.size() / 2) {
        return uploadFull();
    }
    const auto useFullUpload = [&] {
        std::cerr << "Cannot allocate or map mesh scatter staging, using full instance uploads\n";
        m_scatterUnavailable = true;
        return uploadFull();
    };
    if (m_dirtyInstances.size() > m_scatterCapacity) {
        const auto capacity = std::bit_ceil(m_dirtyInstances.size());
        if (capacity > std::numeric_limits<UINT>::max() / sizeof(DirtyGpuInstance)) { return uploadFull(); }
        // Typed SRVs allow CPU writes on D3D11 dynamic buffers. Structured
        // buffers require a misc flag that cannot be combined with CPU access.
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(capacity * sizeof(DirtyGpuInstance));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SHADER_RESOURCE_VIEW_DESC input{};
        input.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        input.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        input.Buffer.NumElements = desc.ByteWidth / 16;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device.CreateBuffer(&desc, nullptr, buffer.GetAddressOf())) ||
            FAILED(device.CreateShaderResourceView(buffer.Get(), &input, view.GetAddressOf()))) { return useFullUpload(); }
        m_scatterBuffer = std::move(buffer);
        m_scatterView = std::move(view);
        m_scatterCapacity = capacity;
    }
    if (!m_scatterCb) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device.CreateBuffer(&desc, nullptr, m_scatterCb.GetAddressOf()))) { return useFullUpload(); }
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.Map(m_scatterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return useFullUpload(); }
    std::memcpy(mapped.pData, m_dirtyInstances.data(), m_dirtyInstances.size() * sizeof(DirtyGpuInstance));
    context.Unmap(m_scatterBuffer.Get(), 0);
    const auto count = static_cast<unsigned>(m_dirtyInstances.size());
    const unsigned constants[]{count, 0, 0, 0};
    context.UpdateSubresource(m_scatterCb.Get(), 0, nullptr, constants, 0, 0);
    context.CSSetShaderResources(0, 1, m_scatterView.GetAddressOf());
    context.CSSetUnorderedAccessViews(0, 1, m_instanceBuffer.output.GetAddressOf(), nullptr);
    context.CSSetConstantBuffers(0, 1, m_scatterCb.GetAddressOf());
    context.CSSetShader(shaders.compute(ComputeShaderId::ScatterInstances), nullptr, 0);
    context.Dispatch((count + kThreads - 1) / kThreads, 1, 1);
    ID3D11ShaderResourceView* nullInput = nullptr;
    ID3D11UnorderedAccessView* nullOutput = nullptr;
    context.CSSetShaderResources(0, 1, &nullInput);
    context.CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
    context.CSSetShader(nullptr, nullptr, 0);
    return true;
}

bool GpuScene::resolvePalettes(const SceneDrawList& list, std::size_t animatedCount) {
    m_fallbackPalettes.clear();
    bool changed = false;
    std::unordered_map<const GpuMesh*, unsigned> restOffsets;
    for (const auto index : m_skinnedObjects) {
        auto& object = m_objects[index];
        const auto rest = std::span<const Mat4>(object.mesh->restPalette);
        const auto* entity = list.scene ? list.scene->entity(m_sources[object.sourceIndex].entityIndex) : nullptr;
        const auto pose = entity && list.animation ? list.animation->meshPose(*entity) : AnimationMeshPose{};
        const bool wasAnimated = object.animated;
        const auto oldHandle = object.animationHandle;
        const auto oldOffset = object.paletteOffset;
        object.animated = !pose.palette.empty() && pose.palette.size() >= rest.size();
        object.animationHandle = object.animated ? pose.handle : AnimationInstanceHandle{};
        if (object.animated) {
            object.paletteOffset = pose.paletteOffset;
            object.animatedBounds = pose.bounds;
        } else if (!list.scene && object.meshInstance.palette && object.meshInstance.paletteCount &&
            object.meshInstance.paletteCount >= rest.size()) {
            object.paletteOffset = static_cast<unsigned>(animatedCount + m_fallbackPalettes.size());
            m_fallbackPalettes.insert(m_fallbackPalettes.end(), object.meshInstance.palette,
                object.meshInstance.palette + object.meshInstance.paletteCount);
            object.animatedBounds = {};
        } else {
            const auto [entry, inserted] = restOffsets.try_emplace(object.mesh,
                static_cast<unsigned>(animatedCount + m_fallbackPalettes.size()));
            if (inserted) { m_fallbackPalettes.insert(m_fallbackPalettes.end(), rest.begin(), rest.end()); }
            object.paletteOffset = entry->second;
            object.animatedBounds = object.mesh->restBounds;
        }
        const bool bindingChanged = wasAnimated != object.animated || oldHandle != object.animationHandle ||
            oldOffset != object.paletteOffset;
        object.boundsDirty |= bindingChanged;
        changed |= bindingChanged;
    }
    return changed;
}

bool GpuScene::skinVertices(ID3D11DeviceContext& context, ID3D11Device& device, ShaderLibrary& shaders,
    const SceneDrawList& list) {
    if (m_skinnedObjects.empty()) { return true; }
    const auto* animation = list.scene ? list.animation : nullptr;
    const auto layout = animation ? animation->layoutRevision() : 0;
    const auto pose = animation ? animation->poseRevision() : 0;
    m_skinLayoutDirty |= m_animation != animation || m_layoutRevision != layout;
    const auto animated = animation ? animation->palettes() : std::span<const Mat4>{};
    if (m_skinLayoutDirty || m_skinBindingsDirty) {
        m_skinLayoutDirty |= resolvePalettes(list, animated.size());
        m_skinBindingsDirty = false;
    }
    const bool layoutChanged = m_skinLayoutDirty;
    const bool shaderChanged = m_skinShaderRevision != shaders.revision();
    const bool poseChanged = m_poseRevision != pose;
    if (!layoutChanged && !poseChanged && !shaderChanged) { return true; }

    auto* shader = shaders.compute(ComputeShaderId::SkinVertices);
    if (!shader) { return false; }
    if (!m_skinDispatchCb) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(SkinDispatchCb);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device.CreateBuffer(&desc, nullptr, m_skinDispatchCb.GetAddressOf()))) { return false; }
    }
    if (layoutChanged) {
        if (animated.size() + m_fallbackPalettes.size() == 0 ||
            !m_paletteBuffer.ensure(device, animated.size() + m_fallbackPalettes.size(), sizeof(Mat4)) ||
            !m_skinnedVertices.ensure(device, m_skinnedVertexCount, sizeof(SkinnedVertex), true)) { return false; }
        if (!m_fallbackPalettes.empty()) {
            m_paletteBuffer.upload(context, m_fallbackPalettes.data(), static_cast<UINT>(m_fallbackPalettes.size() * sizeof(Mat4)),
                static_cast<UINT>(animated.size_bytes()));
        }
    }
    if ((layoutChanged || poseChanged) && !animated.empty()) {
        m_paletteBuffer.upload(context, animated.data(), static_cast<UINT>(animated.size_bytes()));
    }

    // The vertex SRV was unbound before sync. Unbind the UAV before the draw passes pull from it.
    context.CSSetShader(shader, nullptr, 0);
    context.CSSetConstantBuffers(0, 1, m_skinDispatchCb.GetAddressOf());
    context.CSSetUnorderedAccessViews(0, 1, m_skinnedVertices.output.GetAddressOf(), nullptr);
    context.CSSetShaderResources(2, 1, m_paletteBuffer.view.GetAddressOf());
    for (const auto index : m_skinnedObjects) {
        auto& object = m_objects[index];
        const auto version = object.animated ? animation->poseRevision(object.animationHandle) : 0;
        const bool updated = object.animated && version != object.skinnedPoseRevision;
        if (!layoutChanged && !shaderChanged && !updated) { continue; }
        if (updated) {
            object.animatedBounds = animation->bounds(object.animationHandle);
            object.boundsDirty = true;
        }
        const auto& mesh = *object.mesh;
        ID3D11ShaderResourceView* inputs[] = {mesh.skinVertices, mesh.skinWeights};
        context.CSSetShaderResources(0, 2, inputs);
        for (unsigned first = 0; first < mesh.vertexCount;) {
            const auto count = std::min(kDispatchLimit, mesh.vertexCount - first);
            const SkinDispatchCb constants{first, count, object.paletteOffset, object.skinnedVertexBase};
            context.UpdateSubresource(m_skinDispatchCb.Get(), 0, nullptr, &constants, 0, 0);
            context.Dispatch((count + kThreads - 1) / kThreads, 1, 1);
            first += count;
        }
        object.skinnedPoseRevision = version;
    }
    ID3D11UnorderedAccessView* nullOutput = nullptr;
    ID3D11ShaderResourceView* nullInputs[3]{};
    ID3D11Buffer* nullBuffer = nullptr;
    context.CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
    context.CSSetShaderResources(0, 3, nullInputs);
    context.CSSetConstantBuffers(0, 1, &nullBuffer);
    context.CSSetShader(nullptr, nullptr, 0);
    m_animation = animation;
    m_poseRevision = pose;
    m_layoutRevision = layout;
    m_skinShaderRevision = shaders.revision();
    m_skinLayoutDirty = false;
    return true;
}

bool GpuScene::sync(ID3D11DeviceContext& context, ShaderLibrary& shaders, GpuAssetCache& assets,
    const GpuMesh& cube, const SceneDrawList& list, std::uint64_t assetRevision, DVec3 origin, bool prepareAssets, std::uint64_t frame) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context.GetDevice(device.GetAddressOf());
    bool topology = !list.scene || list.scene != m_scene || assetRevision != m_assetRevision;
    if (list.scene && (topology || m_entities != list.scene->entities().data() ||
            m_entityCount != list.scene->entityCount() || list.scene->editRevision() != m_editRevision ||
            list.scene->destructionRevision() != m_destructionRevision)) {
        const auto& entities = list.scene->entities();
        if (!topology) {
            std::size_t slot = 0;
            for (std::size_t i = 0; i < entities.size(); ++i) {
                const auto& entity = entities[i];
                if (!entity.hasMesh) { continue; }
                if (slot == m_sources.size()) { topology = true; break; }
                auto& source = m_sources[slot++];
                const auto& mesh = entity.mesh;
                if (source.id != entity.id() || source.meshNode != mesh.meshNode ||
                    source.combineStatic != mesh.combineStatic || source.meshPath != mesh.meshPath ||
                    source.materialPath != mesh.materialPath) {
                    topology = true;
                    break;
                }
                source.entityIndex = i;
            }
            topology |= slot != m_sources.size();
        }
        if (topology) {
            m_sources.clear();
            for (std::size_t i = 0; i < entities.size(); ++i) {
                const auto& entity = entities[i];
                if (entity.hasMesh) {
                    m_sources.push_back({i, entity.id(), entity.mesh.meshPath, entity.mesh.materialPath,
                        entity.mesh.meshNode, entity.mesh.combineStatic});
                }
            }
        }
        m_skinBindingsDirty = true;
        m_entities = entities.data();
        m_entityCount = entities.size();
        m_editRevision = list.scene->editRevision();
        m_destructionRevision = list.scene->destructionRevision();
    }
    if (topology || m_environment != list.environmentPath) {
        if (prepareAssets) { assets.prepare(device.Get(), list, &context); }
        m_environment = list.environmentPath;
    }
    m_scene = list.scene;
    m_assetRevision = assetRevision;
    if (topology) { rebuild(assets, cube, list); }
    if (m_shaderRevision != shaders.revision()) {
        m_pixels.clear();
        for (const auto& item : m_ranges) {
            const auto* material = item.range.material;
            const auto [entry, inserted] = m_pixels.try_emplace(material);
            if (inserted && !material->shaderPath.empty()) {
                entry->second = prepareAssets ? shaders.projectPixel(device.Get(), material->shaderPath) :
                    shaders.cachedProjectPixel(material->shaderPath);
            }
        }
        m_shaderRevision = shaders.revision();
    }
    if (!skinVertices(context, *device.Get(), shaders, list)) { return false; }

    m_dirtyInstances.clear();
    m_dirtyStaticSources.clear();
    bool changed = topology;
    for (auto& object : m_objects) {
        const bool moved = !object.initialized || (list.scene &&
            !sameTransform(list.scene->entities()[m_sources[object.sourceIndex].entityIndex].transform, object.transform));
        if (!moved && !m_allDirty && !object.boundsDirty) { continue; }
        object.boundsDirty = false;
        if (list.scene && moved) {
            object.transform = list.scene->entities()[m_sources[object.sourceIndex].entityIndex].transform;
            object.meshInstance.world = matrixFromTRS({}, object.transform.rotation, object.transform.scale);
            object.meshInstance.worldPosition = object.transform.position;
        }
        if (moved) {
            for (unsigned i = 0; i < object.staticSourceCount; ++i) {
                m_dirtyStaticSources.push_back(object.firstStaticSource + i);
            }
        }
        changed = true;
        Mat4 world = object.meshInstance.world;
        DVec3 position = list.scene ? object.transform.position : origin + translationOf(world);
        world.m[12] = world.m[13] = world.m[14] = 0;
        world = meshWorld(world, object.node);
        position = position + translationOf(world);
        world.m[12] = world.m[13] = world.m[14] = 0;
        const auto& mesh = *object.mesh;
        Vec3 center{mesh.boundsCenter[0], mesh.boundsCenter[1], mesh.boundsCenter[2]};
        float radius = mesh.boundsRadius;
        if (object.node) {
            center = (object.node->minimum + object.node->maximum) * 0.5f;
            radius = length(object.node->maximum - center);
        }
        if (object.animatedBounds.valid) {
            center = (object.animatedBounds.minimum + object.animatedBounds.maximum) * 0.5f;
            radius = length(object.animatedBounds.maximum - center);
        } else if (mesh.skinVertices) {
            // Keep a skinned object visible if its active pose has no conservative bound.
            radius = -1;
        }
        for (unsigned offset = 0; offset < object.instanceCount; ++offset) {
            const unsigned index = object.firstInstance + offset;
            writeInstance(index, encodeInstance(world, position, center, radius, object.skinnedVertexBase,
                m_thicknessScales[index]));
        }
        object.initialized = true;
    }
    bool staticChanged = false;
    if (!m_staticSources.empty() && (!m_dirtyStaticSources.empty() || m_staticBatches.pending())) {
        const bool budgetAvailable = !list.scene || m_staticFrame != frame;
        staticChanged = m_staticBatches.update(context, assets, m_staticSources, m_dirtyStaticSources, budgetAvailable, m_combined);
        m_staticFrame = frame;
    }
    if (m_tableDirty || staticChanged) {
        const auto& staticBatches = m_staticBatches.batches();
        const auto first = m_thicknessScales.size();
        m_instances.resize(first + staticBatches.size());
        for (unsigned i = 0; i < staticBatches.size(); ++i) {
            const auto& batch = *staticBatches[i];
            const auto& mesh = batch.mesh;
            writeInstance(static_cast<unsigned>(first) + i, encodeInstance(Mat4{}, batch.instance.worldPosition,
                {mesh.boundsCenter[0], mesh.boundsCenter[1], mesh.boundsCenter[2]}, mesh.boundsRadius, 0, {1, 1, 1}));
        }
        buildDrawTable();
        changed = true;
    }
    // A failed submission retries the complete instance upload even if entity
    // transforms are unchanged on the next frame.
    if (!uploadTable(context, *device.Get()) || !uploadInstances(context, *device.Get(), shaders)) {
        m_allDirty = true;
        return false;
    }
    m_allDirty = false;
    m_revision += changed && !m_observedDirty;
    m_observedDirty = false;
    return true;
}

std::uint64_t GpuScene::observeRevision(const Scene& scene) {
    if (m_observedDirty) { return m_revision; }
    bool dirty = m_scene != &scene || scene.entities().data() != m_entities ||
        scene.entityCount() != m_entityCount || scene.editRevision() != m_editRevision ||
        scene.destructionRevision() != m_destructionRevision || m_allDirty || m_staticBatches.pending();
    if (!dirty) {
        const auto& entities = scene.entities();
        dirty = std::any_of(m_objects.begin(), m_objects.end(), [&](const Object& object) {
            return !sameTransform(entities[m_sources[object.sourceIndex].entityIndex].transform, object.transform);
        });
    }
    m_observedDirty = dirty;
    m_revision += dirty;
    return m_revision;
}

void GpuScene::writeInstance(unsigned index, const GpuInstance& data) {
    if (m_allDirty || std::memcmp(&data, &m_instances[index], sizeof(data)) != 0) {
        m_instances[index] = data;
        if (!m_allDirty) {
            m_dirtyInstances.push_back({index, {}, data});
        }
    }
}

GpuScene::ItemRange GpuScene::itemRange(MeshQueue queue) const {
    return queue == MeshQueue::Transmission ? m_transmissionItems : m_opaqueItems;
}

ID3D11PixelShader* GpuScene::customPixel(const GpuMaterial* material) const {
    const auto found = m_pixels.find(material);
    return found == m_pixels.end() ? nullptr : found->second;
}

std::optional<unsigned> GpuScene::skinnedVertexBase(EntityId id) const {
    const auto found = m_objectIndices.find(id);
    if (found == m_objectIndices.end() || !m_objects[found->second].mesh->skinVertices) { return std::nullopt; }
    return m_objects[found->second].skinnedVertexBase;
}

} // namespace sat
