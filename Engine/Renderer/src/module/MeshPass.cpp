#include "MeshPass.hpp"

#include "CascadedShadowMap.hpp"
#include "Renderer.hpp"
#include "ShaderLayout.hpp"
#include "StaticMeshBatches.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include "AssetData.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>
#include <limits>
#include <vector>
#include <utility>
#include <iostream>

namespace sat {

namespace {

static_assert(sizeof(MeshVertex) == 12 * sizeof(float));
constexpr std::size_t kInitialInstanceCapacity = 128;
constexpr std::size_t kMaxInstanceCapacity = 16384;


template <typename T>
void releaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}








void fillUnitCube(MeshVertex* vertices, unsigned short* indices) {
    const float p[8][3] = {
        {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,0.5f,0.5f}, {-0.5f,0.5f,0.5f}
    };
    const int faces[6][4] = {{0,1,2,3}, {4,7,6,5}, {0,3,7,4}, {1,5,6,2}, {0,4,5,1}, {3,2,6,7}};
    const float uv[4][2] = {{0,1}, {1,1}, {1,0}, {0,0}};
    for (int face = 0; face < 6; ++face) {
        const auto* a = p[faces[face][0]];
        const auto* b = p[faces[face][1]];
        const auto* c = p[faces[face][3]];
        const Vec3 tangent{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const Vec3 bitangent{a[0] - c[0], a[1] - c[1], a[2] - c[2]};
        const Vec3 normal = cross(tangent, bitangent);
        for (int corner = 0; corner < 4; ++corner) {
            const auto* position = p[faces[face][corner]];
            vertices[face * 4 + corner] = {position[0], position[1], position[2], normal.x, normal.y, normal.z,
                uv[corner][0], uv[corner][1], tangent.x, tangent.y, tangent.z, 1.0f};
        }
        const auto base = static_cast<unsigned short>(face * 4);
        // Exterior faces are clockwise with the pass's D3D11 rasterizer.
        const unsigned short triangle[6] = {base, static_cast<unsigned short>(base + 2), static_cast<unsigned short>(base + 1),
            base, static_cast<unsigned short>(base + 3), static_cast<unsigned short>(base + 2)};
        for (int i = 0; i < 6; ++i) { indices[face * 6 + i] = triangle[i]; }
    }
}

} // namespace

struct MeshPass::Impl {
    struct DrawRange {
        const MeshInstance* instance;
        const GpuMesh* mesh;
        const GpuMaterial* material;
        UINT firstIndex;
        UINT indexCount;
        UINT vertexCount;
        const GpuMeshNode* node = nullptr;

        [[nodiscard]] bool canCombine() const {
            return indexCount != 0 && !instance->meshPath.empty() &&
                (node ? mesh->mergeNodeDraws : instance->combineStatic);
        }

        [[nodiscard]] bool sameBatch(const DrawRange& other) const {
            return mesh == other.mesh && material == other.material &&
                firstIndex == other.firstIndex && indexCount == other.indexCount;
        }
        [[nodiscard]] static bool batchLess(const DrawRange& a, const DrawRange& b) {
            if (a.material != b.material) { return std::less<>{}(a.material, b.material); }
            if (a.mesh != b.mesh) { return std::less<>{}(a.mesh, b.mesh); }
            if (a.firstIndex != b.firstIndex) { return a.firstIndex < b.firstIndex; }
            return a.indexCount < b.indexCount;
        }
        [[nodiscard]] static bool opaqueLess(const DrawRange& a, const DrawRange& b) {
            if (a.material != b.material && a.material->shaderPath != b.material->shaderPath) {
                return a.material->shaderPath < b.material->shaderPath;
            }
            return batchLess(a, b);
        }
    };
    struct ShadowCaster {
        DrawRange draw;
        Vec3 lightCenter;
        float radius;
    };
    struct TransparentDraw {
        DrawRange draw;
        float depth;
    };

    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Buffer> instanceStream;
    std::size_t instanceCapacity = 0;
    bool instanceUploadFailed = false;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> transparentDepth;
    std::vector<TransparentDraw> transparent;
    std::vector<DrawRange> opaque;
    std::vector<DrawRange> visibleCasters;
    StaticMeshBatches sceneBatches;
    StaticMeshBatches gameBatches;
    std::vector<StaticMeshSource> staticSources;
    std::vector<unsigned char> combinedDraws;
    CascadedShadowMap shadowMap;
    std::vector<ShadowCaster> shadowCasters;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> environmentSampler;
    GpuMesh cubeMesh{};
    ID3D11Buffer* lightsCb = nullptr;
    ID3D11Buffer* clusterCb = nullptr;
    std::vector<const LightInstance*> lightOrder;
    bool ready = false;
    bool failed = false;

    [[nodiscard]] bool growInstanceStream(ID3D11Device& device, std::size_t capacity) {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = static_cast<UINT>(capacity * sizeof(MeshInstanceData));
        Microsoft::WRL::ComPtr<ID3D11Buffer> candidate;
        if (FAILED(device.CreateBuffer(&desc, nullptr, candidate.GetAddressOf()))) { return false; }
        instanceStream = std::move(candidate);
        instanceCapacity = capacity;
        return true;
    }
};

MeshPass::~MeshPass() {
    release();
}

void MeshPass::release() {
    if (m_impl == nullptr) {
        return;
    }
    releaseCom(m_impl->lightsCb);
    releaseCom(m_impl->clusterCb);
    releaseCom(m_impl->depthState);
    releaseCom(m_impl->rasterizer);
    releaseCom(m_impl->frameCb);
    releaseCom(m_impl->ib);
    releaseCom(m_impl->vb);
    delete m_impl;
    m_impl = nullptr;
}

void MeshPass::setAssetReader(AssetReader reader) {
    m_assets.setReader(std::move(reader));
    ++m_assetRevision;
}
void MeshPass::prepareAssets(ID3D11Device* device, const SceneDrawList& drawList) { m_assets.prepare(device, drawList); }

const GpuMesh* MeshPass::geometry(const MeshInstance& instance) const {
    if (m_impl == nullptr || !m_impl->ready) {
        return nullptr;
    }
    if (!instance.meshPath.empty()) { return m_assets.mesh(instance.meshPath); }
    switch (instance.primitive) {
    case MeshPrimitive::Cube:
        return m_impl->cubeMesh.valid() ? &m_impl->cubeMesh : nullptr;
    }
    return nullptr;
}

bool MeshPass::ensure(ID3D11Device* device) {
    if (m_impl != nullptr && m_impl->ready) {
        return true;
    }
    if (device == nullptr) {
        return false;
    }
    if (m_impl == nullptr) {
        m_impl = new Impl();
    }
    if (m_impl->failed) {
        return false;
    }

    HRESULT hr = S_OK;


    MeshVertex vertices[24];
    unsigned short indices[36];
    fillUnitCube(vertices, indices);

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = vertices;
    hr = device->CreateBuffer(&vbDesc, &vbData, &m_impl->vb);
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData{};
    ibData.pSysMem = indices;
    hr = device->CreateBuffer(&ibDesc, &ibData, &m_impl->ib);
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    m_impl->cubeMesh.vertexBuffer = m_impl->vb;
    m_impl->cubeMesh.indexBuffer = m_impl->ib;
    m_impl->cubeMesh.indexCount = static_cast<UINT>(sizeof(indices) / sizeof(indices[0]));
    m_impl->cubeMesh.vertexCount = static_cast<UINT>(sizeof(vertices) / sizeof(vertices[0]));
    m_impl->cubeMesh.vertexStride = sizeof(MeshVertex);
    m_impl->cubeMesh.indexFormat = DXGI_FORMAT_R16_UINT;
    m_impl->cubeMesh.boundsRadius = std::sqrt(3.0f) * 0.5f;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(MeshFrameCb);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_impl->frameCb);
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    if (!m_impl->growInstanceStream(*device, kInitialInstanceCapacity)) {
        std::cerr << "Cannot create mesh instance stream\n";
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    cbDesc.ByteWidth = sizeof(LightsCb);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_impl->lightsCb);
    cbDesc.ByteWidth = sizeof(ClusterCb);
    if (FAILED(hr) || FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_impl->clusterCb))) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_BACK;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&raster, &m_impl->rasterizer);
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    hr = device->CreateDepthStencilState(&depth, &m_impl->depthState);
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    D3D11_BLEND_DESC blend{};
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateDepthStencilState(&depth, m_impl->transparentDepth.GetAddressOf())) ||
        FAILED(device->CreateBlendState(&blend, m_impl->blend.GetAddressOf()))) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    D3D11_SAMPLER_DESC environmentSampler{};
    environmentSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    environmentSampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    environmentSampler.AddressV = environmentSampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    environmentSampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    environmentSampler.MaxAnisotropy = 1;
    environmentSampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&environmentSampler, m_impl->environmentSampler.GetAddressOf()))) {
        std::cerr << "Cannot create environment sampler\n";
    }

    m_impl->ready = true;
    return true;
}

void MeshPass::draw(
    ID3D11DeviceContext* context, ShaderProgram program,
    const Mat4& view, const Mat4& projection, Vec3 eye,
    const SceneDrawList& drawList, bool hdr, ID3D11PixelShader* shadowPixel, ShaderLibrary* shaders,
    DVec3 renderOrigin, bool loadProjectShaders, GameViewStats* stats) {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr || drawList.meshes == nullptr ||
        drawList.meshCount <= 0) {
        return;
    }

    const Mat4 viewProj = projection * view;
    const int lightCount = drawList.lights ? std::clamp(drawList.lightCount, 0, kMaxLights) : 0;
    auto& order = m_impl->lightOrder;
    order.clear();
    if (lightCount != 0 && drawList.lightCount > kMaxLights) {
        order.reserve(static_cast<std::size_t>(drawList.lightCount));
        for (int i = 0; i < drawList.lightCount; ++i) { order.push_back(&drawList.lights[i]); }
        std::partial_sort(order.begin(), order.begin() + kMaxLights, order.end(), [&](const auto* a, const auto* b) {
            const bool aDirectional = a->type == LightType::Directional;
            const bool bDirectional = b->type == LightType::Directional;
            if (aDirectional != bDirectional) { return aDirectional; }
            if (aDirectional) { return a < b; }
            const Vec3 da = a->position - eye, db = b->position - eye;
            const float aa = dot(da, da), bb = dot(db, db);
            const float distanceA = std::isfinite(aa) ? aa : std::numeric_limits<float>::max();
            const float distanceB = std::isfinite(bb) ? bb : std::numeric_limits<float>::max();
            return distanceA == distanceB ? a < b : distanceA < distanceB;
        });
    }
    D3D11_VIEWPORT viewport{};
    UINT viewportCount = 1;
    context->RSGetViewports(&viewportCount, &viewport);
    if (viewportCount == 0 || viewport.Width <= 0 || viewport.Height <= 0) { return; }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    MeshFrameCb frame{};
    auto* environment = m_assets.environment(drawList.environmentPath);
    frame.environmentIntensity = environment && m_impl->environmentSampler ? drawList.environmentIntensity : 0.0f;
    frame.shadowLight = -1;
    auto& shadowMap = m_impl->shadowMap;
    if (drawList.directionalShadows) {
        for (int i = 0; i < lightCount; ++i) {
            const auto& light = order.empty() ? drawList.lights[i] : *order[i];
            if (light.type != LightType::Directional || dot(light.radiance, light.radiance) == 0) { continue; }
            if (shadowMap.update(context, view, projection, renderOrigin, light.direction, drawList)) {
                frame.shadowLight = i;
                frame.cascadeCount = shadowMap.count();
                frame.cascadeBlend = CascadedShadowMap::kBlendFraction;
                frame.cascadeNear = shadowMap.nearDepth();
                if (projection.m[15] == 1.0f) {
                    frame.orthoDepthScale = 1.0f / projection.m[10];
                }
                for (unsigned cascade = 0; cascade < frame.cascadeCount; ++cascade) {
                    const auto& fitted = shadowMap.cascade(cascade);
                    std::memcpy(frame.cascadeViewProj[cascade], fitted.viewProj.m, sizeof(fitted.viewProj.m));
                    frame.cascadeSplits[cascade] = fitted.split;
                    frame.cascadeTexel[cascade] = fitted.texelSize;
                }
            }
            break;
        }
    }
    std::memcpy(frame.viewProj, viewProj.m, sizeof(frame.viewProj));
    frame.eye[0] = eye.x; frame.eye[1] = eye.y; frame.eye[2] = eye.z;
    frame.viewport[0] = viewport.TopLeftX;
    frame.viewport[1] = viewport.TopLeftY;
    frame.viewport[2] = kClusterTiles / viewport.Width;
    frame.viewport[3] = kClusterTiles / viewport.Height;
    frame.lightCount = static_cast<unsigned>(lightCount);
    frame.ambient[0] = drawList.ambient.x; frame.ambient[1] = drawList.ambient.y;
    frame.ambient[2] = drawList.ambient.z; frame.ambient[3] = hdr ? 1.0f : 0.0f;
    if (lightCount > 0) {
        if (FAILED(context->Map(m_impl->lightsCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
        auto* packed = static_cast<LightsCb*>(mapped.pData);
        for (int i = 0; i < lightCount; ++i) {
            const auto& light = order.empty() ? drawList.lights[i] : *order[i];
            packed->lights[i] = {{light.position.x, light.position.y, light.position.z}, static_cast<float>(light.type),
                {light.direction.x, light.direction.y, light.direction.z}, light.range,
                {light.radiance.x, light.radiance.y, light.radiance.z}, light.innerCos, light.outerCos, {}};
        }
        context->Unmap(m_impl->lightsCb, 0);
    }
    if (FAILED(context->Map(m_impl->clusterCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
    ClusterCb clusters{};
    const auto& vp = viewProj.m;
    const Vec3 rows[] = {{vp[0], vp[4], vp[8]}, {vp[1], vp[5], vp[9]}, {vp[3], vp[7], vp[11]}};
    const float rowLength[] = {length(rows[0]), length(rows[1]), length(rows[2])};
    const auto slice = [](float w) {
        return static_cast<int>(std::clamp(std::log2(std::max(w, 0.125f)) + 3.0f, 0.0f, 7.0f));
    };
    for (int i = 0; i < lightCount; ++i) {
        const auto& light = order.empty() ? drawList.lights[i] : *order[i];
        int minX = 0, minY = 0, minZ = 0;
        int maxX = kClusterTiles - 1, maxY = maxX, maxZ = kClusterSlices - 1;
        if (light.type != LightType::Directional) {
            const float w = dot(rows[2], light.position) + vp[15];
            const float wRadius = rowLength[2] * light.range;
            const float nearW = w - wRadius, farW = w + wRadius;
            if (std::isfinite(nearW) && std::isfinite(farW)) {
                if (farW <= 0) { continue; }
                minZ = slice(nearW);
                maxZ = slice(farW);
                if (nearW > 0) {
                    const auto tileBounds = [&](float center, float radius, int& first, int& last) {
                        const float a = center - radius, b = center + radius;
                        const float lo = std::min(a / nearW, a / farW);
                        const float hi = std::max(b / nearW, b / farW);
                        if (std::isfinite(lo) && std::isfinite(hi)) {
                            first = static_cast<int>(std::clamp((lo * 0.5f + 0.5f) * kClusterTiles, 0.0f, 7.0f));
                            last = static_cast<int>(std::clamp((hi * 0.5f + 0.5f) * kClusterTiles, 0.0f, 7.0f));
                        }
                    };
                    tileBounds(dot(rows[0], light.position) + vp[12], rowLength[0] * light.range, minX, maxX);
                    tileBounds(-dot(rows[1], light.position) - vp[13], rowLength[1] * light.range, minY, maxY);
                }
            }
        }
        const unsigned bit = 1u << (i % 32);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) { clusters.masks[z][y][x][i / 32] |= bit; }
            }
        }
    }
    std::memcpy(mapped.pData, &clusters, sizeof(clusters));
    context->Unmap(m_impl->clusterCb, 0);
    context->IASetInputLayout(program.layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(program.vertex, nullptr, 0);
    context->PSSetShader(program.pixel, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->PSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->PSSetConstantBuffers(3, 1, &m_impl->lightsCb);
    context->PSSetConstantBuffers(4, 1, &m_impl->clusterCb);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);

    const GpuMesh* boundMesh = nullptr;
    const GpuMaterial* boundMaterial = nullptr;
    bool shadowPass = false;
    bool passChanged = true;
    ID3D11PixelShader* boundPixel = program.pixel;
    std::string_view boundShaderPath;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    const auto getDevice = [&]() {
        if (!device) { context->GetDevice(device.GetAddressOf()); }
        return device.Get();
    };
    const auto drawMesh = [&](const Impl::DrawRange& draw, UINT count, UINT firstInstance) {
        const auto* mesh = draw.mesh;
        const auto* material = draw.material;
        if (mesh != boundMesh) {
            const UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &mesh->vertexBuffer, &mesh->vertexStride, &offset);
            context->IASetIndexBuffer(mesh->indexBuffer, mesh->indexFormat, 0);
            boundMesh = mesh;
        }
        if (shadowPass || (material != boundMaterial && material->shaderPath != boundShaderPath)) {
            auto* pixel = program.pixel;
            if (shadowPass) {
                pixel = material->alphaMode == AlphaMode::Mask ? shadowPixel : nullptr;
            } else if (shaders && !material->shaderPath.empty()) {
                auto* custom = loadProjectShaders ? shaders->projectPixel(getDevice(), material->shaderPath)
                                                  : shaders->cachedProjectPixel(material->shaderPath);
                if (custom) { pixel = custom; }
            }
            if (!shadowPass) { boundShaderPath = material->shaderPath; }
            if (pixel != boundPixel) {
                context->PSSetShader(pixel, nullptr, 0);
                boundPixel = pixel;
                passChanged = true;
            }
        }
        if (material != boundMaterial && (!shadowPass || material->alphaMode == AlphaMode::Mask)) {
            context->PSSetConstantBuffers(2, 1, &material->constants);
            if (!boundMaterial || material->sampler != boundMaterial->sampler) {
                context->PSSetSamplers(0, 1, &material->sampler);
            }
            if (!boundMaterial || !std::equal(material->views, material->views + 4, boundMaterial->views)) {
                context->PSSetShaderResources(0, 4, material->views);
            }
            if (!shadowPass && (!boundMaterial || material->occlusion != boundMaterial->occlusion)) {
                context->PSSetShaderResources(6, 1, &material->occlusion);
            }
            boundMaterial = material;
            passChanged = true;
        }
        context->DrawIndexedInstanced(draw.indexCount, count, draw.firstIndex, 0, firstInstance);
        if (stats) {
            ++stats->drawCalls;
            stats->setPassCalls += passChanged;
            stats->triangles += static_cast<std::uint64_t>(draw.indexCount / 3) * count;
            stats->vertices += static_cast<std::uint64_t>(draw.vertexCount) * count;
            passChanged = false;
        }
    };
    const auto submit = [&](const auto& queue, auto drawRange, bool allowInstancing) {
        if (queue.empty()) { return true; }
        const auto requested = std::min(queue.size(), kMaxInstanceCapacity);
        if (requested > m_impl->instanceCapacity) {
            // The existing stream remains usable in smaller chunks if growth fails.
            (void)m_impl->growInstanceStream(*getDevice(), std::bit_ceil(requested));
        }
        const UINT stride = sizeof(MeshInstanceData);
        const UINT offset = 0;
        context->IASetVertexBuffers(1, 1, m_impl->instanceStream.GetAddressOf(), &stride, &offset);
        for (std::size_t first = 0; first < queue.size();) {
            const auto size = std::min(queue.size() - first, m_impl->instanceCapacity);
            if (FAILED(context->Map(m_impl->instanceStream.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                if (!m_impl->instanceUploadFailed) { std::cerr << "Cannot upload mesh instances\n"; }
                m_impl->instanceUploadFailed = true;
                return false;
            }
            auto* instances = static_cast<MeshInstanceData*>(mapped.pData);
            for (std::size_t i = 0; i < size; ++i) {
                const auto& draw = std::invoke(drawRange, queue[first + i]);
                writeMeshInstance(instances[i], meshWorld(draw.instance->world, draw.node));
            }
            context->Unmap(m_impl->instanceStream.Get(), 0);
            m_impl->instanceUploadFailed = false;

            for (std::size_t i = 0; i < size;) {
                const auto& draw = std::invoke(drawRange, queue[first + i]);
                std::size_t count = 1;
                while (allowInstancing && i + count < size &&
                    draw.sameBatch(std::invoke(drawRange, queue[first + i + count]))) {
                    ++count;
                }
                drawMesh(draw, static_cast<UINT>(count), static_cast<UINT>(i));
                i += count;
            }
            first += size;
        }
        return true;
    };
    const auto visitSections = [&](const MeshInstance& instance, const GpuMesh* mesh, const auto& visit) {
        const auto* material = m_assets.material(instance.materialPath);
        const auto* node = mesh->node(instance.meshNode);
        if (instance.meshNode != 0 && !node) { return; }
        if (mesh->mainSectionCount == 0 || !instance.materialPath.empty()) {
            if (material) {
                const Vec3 center = node ? (node->minimum + node->maximum) * 0.5f : Vec3{};
                visit(Impl::DrawRange{&instance, mesh, material, node ? node->firstIndex : 0,
                    node ? node->indexCount : mesh->indexCount, node ? node->vertexCount : mesh->vertexCount, node}, center);
            }
            return;
        }
        const auto sections = std::span(mesh->sections).subspan(node ? node->firstSection : 0,
            node ? node->sectionCount : mesh->mainSectionCount);
        for (const auto& section : sections) {
            const auto* sectionMaterial = section.material ? section.material : material;
            if (!sectionMaterial) { continue; }
            const auto& range = section.range;
            const auto& center = section.center;
            visit(Impl::DrawRange{&instance, mesh, sectionMaterial, range.firstIndex, range.indexCount, section.vertexCount, node},
                Vec3{center[0], center[1], center[2]});
        }
    };
    if (frame.cascadeCount != 0) {
        auto& casters = m_impl->shadowCasters;
        casters.clear();
        casters.reserve(static_cast<std::size_t>(drawList.meshCount));
        for (int i = 0; i < drawList.meshCount; ++i) {
            const auto& instance = drawList.meshes[i];
            const auto* mesh = geometry(instance);
            if (!mesh || !mesh->valid()) { continue; }

            const auto* node = mesh->node(instance.meshNode);
            if (instance.meshNode != 0 && !node) { continue; }
            const Mat4 world = meshWorld(instance.world, node);
            const Vec3 x{world.m[0], world.m[1], world.m[2]};
            const Vec3 y{world.m[4], world.m[5], world.m[6]};
            const Vec3 z{world.m[8], world.m[9], world.m[10]};
            // Bound the largest squared stretch by the maximum absolute row
            // sum of the Gram matrix. This is the largest squared scale for TRS.
            const float xy = std::abs(dot(x, y)), xz = std::abs(dot(x, z)), yz = std::abs(dot(y, z));
            const float scale = std::sqrt(std::max({dot(x, x) + xy + xz, dot(y, y) + xy + yz, dot(z, z) + xz + yz}));
            const auto& center = mesh->boundsCenter;
            const Vec3 localCenter = node ? (node->minimum + node->maximum) * 0.5f : Vec3{center[0], center[1], center[2]};
            const Vec3 worldCenter = transformPoint(world, localCenter);
            const auto lightCenter = shadowMap.lightSpaceCenter(worldCenter);
            const float radius = (node ? length(node->maximum - localCenter) : mesh->boundsRadius) * scale;
            visitSections(instance, mesh, [&](const Impl::DrawRange& draw, Vec3) {
                const auto mode = draw.material->alphaMode;
                if (mode != AlphaMode::Blend && (mode != AlphaMode::Mask || shadowPixel)) {
                    casters.push_back({draw, lightCenter, radius});
                }
            });
        }

        std::sort(casters.begin(), casters.end(), [](const auto& a, const auto& b) {
            if (a.draw.material->alphaMode != b.draw.material->alphaMode) {
                return a.draw.material->alphaMode < b.draw.material->alphaMode;
            }
            return Impl::DrawRange::batchLess(a.draw, b.draw);
        });
        auto& visible = m_impl->visibleCasters;
        visible.reserve(casters.size());
        shadowPass = true;
        ID3D11ShaderResourceView* unboundShadow = nullptr;
        context->PSSetShaderResources(5, 1, &unboundShadow);
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
        context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
        const float size = static_cast<float>(shadowMap.resolution());
        const D3D11_VIEWPORT shadowViewport{0, 0, size, size, 0, 1};
        context->RSSetViewports(1, &shadowViewport);
        context->RSSetState(shadowMap.rasterizer());
        for (unsigned cascade = 0; cascade < frame.cascadeCount; ++cascade) {
            if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                frame.shadowLight = -1;
                frame.cascadeCount = 0;
                break;
            }
            // The shadow vertex shader only reads the first matrix for position.
            std::memcpy(mapped.pData, frame.cascadeViewProj[cascade], sizeof(frame.viewProj));
            context->Unmap(m_impl->frameCb, 0);

            auto* depthView = shadowMap.depthView(cascade);
            context->OMSetRenderTargets(0, nullptr, depthView);
            context->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, 1, 0);
            passChanged = true;
            visible.clear();
            for (const auto& caster : casters) {
                if (shadowMap.overlaps(cascade, caster.lightCenter, caster.radius)) { visible.push_back(caster.draw); }
            }
            if (!submit(visible, std::identity{}, true)) {
                frame.shadowLight = -1;
                frame.cascadeCount = 0;
                break;
            }
        }
        auto* target = previousTarget.Get();
        context->OMSetRenderTargets(1, &target, previousDepth.Get());
        context->RSSetViewports(1, &viewport);
        context->RSSetState(m_impl->rasterizer);
        context->PSSetShader(program.pixel, nullptr, 0);
        boundPixel = program.pixel;
        shadowPass = false;
        passChanged = true;
        boundMaterial = nullptr;
    }
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
    std::memcpy(mapped.pData, &frame, sizeof(frame));
    context->Unmap(m_impl->frameCb, 0);

    ID3D11ShaderResourceView* lightingViews[] = {environment, frame.cascadeCount != 0 ? shadowMap.shaderView() : nullptr};
    ID3D11SamplerState* lightingSamplers[] = {m_impl->environmentSampler.Get(), shadowMap.sampler()};
    context->PSSetShaderResources(4, 2, lightingViews);
    context->PSSetSamplers(1, 2, lightingSamplers);
    auto& transparent = m_impl->transparent;
    transparent.clear();
    auto& opaque = m_impl->opaque;
    opaque.clear();
    bool hasStaticSources = false;
    for (int i = 0; i < drawList.meshCount; ++i) {
        const auto& instance = drawList.meshes[i];
        const auto* mesh = geometry(instance);
        if (!mesh || !mesh->valid()) { continue; }
        visitSections(instance, mesh, [&](const Impl::DrawRange& draw, Vec3 localCenter) {
            if (draw.material->alphaMode == AlphaMode::Blend) {
                const auto center = transformPoint(meshWorld(instance.world, draw.node), localCenter);
                const float depth = viewProj.m[3] * center.x + viewProj.m[7] * center.y +
                    viewProj.m[11] * center.z + viewProj.m[15];
                transparent.push_back({draw, std::isfinite(depth) ? depth : 0.0f});
            } else {
                opaque.push_back(draw);
                hasStaticSources |= draw.canCombine();
            }
        });
    }
    std::sort(opaque.begin(), opaque.end(), Impl::DrawRange::opaqueLess);
    auto& sources = m_impl->staticSources;
    sources.clear();
    for (std::size_t first = 0; hasStaticSources && first < opaque.size();) {
        const auto& draw = opaque[first];
        std::size_t last = first + 1;
        while (last < opaque.size() && draw.sameBatch(opaque[last])) { ++last; }
        // A repeated glTF node can still combine with other ranges using the same material.
        // Instancing alone would keep a separate draw for each of those ranges.
        for (std::size_t i = first; i < last; ++i) {
            const auto& entry = opaque[i];
            const auto position = entry.instance->worldPosition;
            if ((entry.node || last == first + 1) && entry.canCombine() &&
                std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
                sources.push_back({entry.instance, entry.mesh, entry.material,
                    entry.firstIndex, entry.indexCount, i, entry.node});
            }
        }
        first = last;
    }
    auto& staticBatches = drawList.drawEditorOverlay ? m_impl->sceneBatches : m_impl->gameBatches;
    auto& combined = m_impl->combinedDraws;
    combined.assign(sources.empty() ? 0 : opaque.size(), 0);
    staticBatches.update(*context, m_assets, m_assetRevision, sources, renderOrigin, combined);
    if (!staticBatches.batches().empty()) {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < opaque.size(); ++i) {
            if (!combined[i]) { opaque[kept++] = opaque[i]; }
        }
        opaque.resize(kept);
        for (const auto& batch : staticBatches.batches()) {
            opaque.push_back({&batch.instance, &batch.mesh, batch.material, 0, batch.mesh.indexCount, batch.mesh.vertexCount});
        }
        std::sort(opaque.begin(), opaque.end(), Impl::DrawRange::opaqueLess);
    }
    (void)submit(opaque, std::identity{}, true);
    if (!transparent.empty()) {
        std::sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) {
            if (a.depth != b.depth) { return a.depth > b.depth; }
            if (a.draw.instance != b.draw.instance) { return a.draw.instance < b.draw.instance; }
            return a.draw.firstIndex < b.draw.firstIndex;
        });
        context->OMSetBlendState(m_impl->blend.Get(), nullptr, 0xffffffff);
        context->OMSetDepthStencilState(m_impl->transparentDepth.Get(), 0);
        passChanged = true;
        (void)submit(transparent, &Impl::TransparentDraw::draw, false);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    }
    ID3D11ShaderResourceView* unbound[7]{};
    context->PSSetShaderResources(0, 7, unbound);
}

} // namespace sat
