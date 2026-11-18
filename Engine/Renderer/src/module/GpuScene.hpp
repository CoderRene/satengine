#pragma once

#include "AnimationSystem.hpp"
#include "GpuAssetCache.hpp"
#include "ShaderLayout.hpp"
#include "StaticMeshBatches.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sat {

enum class MeshQueue : unsigned { Opaque = 1, Transmission = 2, Shadow = 4, Transparent = 8 };
constexpr unsigned meshQueueMask(MeshQueue queue) { return static_cast<unsigned>(queue); }
void splitOrigin(DVec3 origin, float* high, float* low);

// Only used on the immediate context. Scene and asset references are borrowed
// during synchronization, and the cached draw table expires on the next sync.
class GpuScene {
public:
    struct DrawRange {
        const GpuMesh* mesh;
        const GpuMaterial* material;
        UINT firstIndex, indexCount, vertexCount;
        unsigned passes;
        std::uint64_t shaderKey = 0;
    };
    struct DrawItem {
        DrawRange range;
        unsigned instance;
        Vec3 localCenter;
    };

    [[nodiscard]] bool sync(ID3D11DeviceContext& context, ShaderLibrary& shaders, GpuAssetCache& assets,
        const GpuMesh& cube, const SceneDrawList& list, std::uint64_t assetRevision, DVec3 origin, bool prepareAssets, std::uint64_t frame);
    [[nodiscard]] const Scene* source() const { return m_scene; }
    [[nodiscard]] const std::vector<DrawRange>& batches() const { return m_batches; }
    [[nodiscard]] const std::vector<DrawItem>& transparent() const { return m_transparent; }
    [[nodiscard]] const GpuInstance& instance(unsigned index) const { return m_instances[index]; }
    struct ItemRange { UINT start = 0, count = 0; };
    [[nodiscard]] ItemRange itemRange(MeshQueue queue) const;
    [[nodiscard]] unsigned itemCount() const { return static_cast<unsigned>(m_items.size()); }
    [[nodiscard]] ID3D11ShaderResourceView* instancesView() const { return m_instanceBuffer.view.Get(); }
    [[nodiscard]] ID3D11ShaderResourceView* itemsView() const { return m_itemBuffer.view.Get(); }
    [[nodiscard]] ID3D11ShaderResourceView* skinnedVerticesView() const { return m_skinnedVertices.view.Get(); }
    [[nodiscard]] ID3D11Buffer* argsTemplate() const { return m_args.Get(); }
    [[nodiscard]] ID3D11PixelShader* customPixel(const GpuMaterial* material) const;
    [[nodiscard]] std::optional<unsigned> skinnedVertexBase(EntityId id) const;
    [[nodiscard]] std::uint64_t observeRevision(const Scene& scene);
    void retryNextFrame() { m_allDirty = true; m_observedDirty = false; }

private:
    struct Structured {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
        std::size_t capacity = 0;
        [[nodiscard]] bool ensure(ID3D11Device& device, std::size_t count, UINT stride, bool writable = false);
        void upload(ID3D11DeviceContext& context, const void* data, UINT bytes, UINT offset = 0) const;
    };
    struct Source {
        std::size_t entityIndex;
        EntityId id;
        std::string meshPath, materialPath;
        unsigned meshNode;
        bool combineStatic;
    };
    struct Object {
        Transform transform;
        MeshInstance meshInstance;
        const GpuMesh* mesh;
        const GpuMeshNode* node;
        std::size_t sourceIndex;
        unsigned firstInstance, instanceCount = 0;
        unsigned firstStaticSource = 0, staticSourceCount = 0;
        unsigned paletteOffset = 0, skinnedVertexBase = 0;
        AnimationInstanceHandle animationHandle;
        std::uint64_t skinnedPoseRevision = 0;
        bool animated = false, boundsDirty = false;
        AnimationBounds animatedBounds{};
        bool initialized = false;
    };
    void rebuild(const GpuAssetCache& assets, const GpuMesh& cube, const SceneDrawList& list);
    void buildDrawTable();
    void writeInstance(unsigned index, const GpuInstance& data);
    [[nodiscard]] bool uploadTable(ID3D11DeviceContext& context, ID3D11Device& device);
    [[nodiscard]] bool uploadInstances(ID3D11DeviceContext& context, ID3D11Device& device, ShaderLibrary& shaders);
    [[nodiscard]] bool skinVertices(ID3D11DeviceContext& context, ID3D11Device& device, ShaderLibrary& shaders,
        const SceneDrawList& list);
    [[nodiscard]] bool resolvePalettes(const SceneDrawList& list, std::size_t animatedCount);

    const Scene* m_scene = nullptr;
    const Entity* m_entities = nullptr;
    std::size_t m_entityCount = 0;
    std::uint64_t m_editRevision = 0, m_destructionRevision = 0, m_assetRevision = 0;
    std::uint64_t m_shaderRevision = ~std::uint64_t{};
    bool m_tableDirty = true;
    bool m_allDirty = true;
    std::string m_environment;
    std::vector<Source> m_sources;
    std::vector<Object> m_objects;
    std::unordered_map<EntityId, unsigned> m_objectIndices;
    std::vector<DrawItem> m_ranges;
    std::vector<unsigned> m_sortedRanges;
    std::vector<unsigned> m_skinnedObjects;
    std::vector<Vec3> m_thicknessScales;
    StaticMeshBatches m_staticBatches;
    std::vector<StaticMeshSource> m_staticSources;
    std::vector<unsigned> m_dirtyStaticSources;
    std::vector<unsigned char> m_combined;
    const AnimationSystem* m_animation = nullptr;
    std::uint64_t m_poseRevision = 0, m_layoutRevision = 0, m_skinShaderRevision = 0;
    std::size_t m_skinnedVertexCount = 0;
    bool m_skinLayoutDirty = true, m_skinBindingsDirty = true;
    std::uint64_t m_revision = 0;
    std::uint64_t m_staticFrame = ~std::uint64_t{};
    bool m_observedDirty = false;
    std::vector<GpuInstance> m_instances;
    std::vector<DrawRange> m_batches;
    std::vector<DrawItem> m_transparent;
    std::vector<GpuBatch> m_templates;
    std::vector<GpuDrawItem> m_items;
    ItemRange m_opaqueItems, m_transmissionItems;
    std::vector<DirtyGpuInstance> m_dirtyInstances;
    std::vector<Mat4> m_fallbackPalettes;
    std::unordered_map<const GpuMaterial*, ID3D11PixelShader*> m_pixels;
    Structured m_instanceBuffer, m_itemBuffer, m_paletteBuffer, m_skinnedVertices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_args, m_scatterCb, m_scatterBuffer, m_skinDispatchCb;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_scatterView;
    std::size_t m_scatterCapacity = 0;
    bool m_scatterUnavailable = false;
};

} // namespace sat
