#include "MeshPass.hpp"

#include "CascadedShadowMap.hpp"
#include "Renderer.hpp"
#include "GpuCulling.hpp"
#include "GpuScene.hpp"
#include "GpuViewStats.hpp"
#include "ShaderLayout.hpp"

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
#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <chrono>
#include <limits>
#include <vector>
#include <utility>
#include <iostream>
#include <iterator>

namespace sat {

namespace {

constexpr UINT kSkinnedVertexSlot = 14;

constexpr UINT kOpaqueSceneSlot = 12;
constexpr UINT kSpotShadowSlot = 15;

void drawFullscreen(ID3D11DeviceContext& context, ShaderProgram program, ID3D11RasterizerState* rasterizer,
    ID3D11DepthStencilState* depth) {
    context.IASetInputLayout(nullptr);
    context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.VSSetShader(program.vertex, nullptr, 0);
    context.PSSetShader(program.pixel, nullptr, 0);
    context.OMSetDepthStencilState(depth, 0);
    context.RSSetState(rasterizer);
    context.Draw(3, 0);
}


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
    struct TransparentDraw {
        const GpuScene::DrawItem* draw;
        float depth;
    };
    GpuScene scenes[2];
    GpuScene transientScene;
    GpuScene* currentScene = nullptr;
    GpuCulling culling[2];
    Microsoft::WRL::ComPtr<ID3D11Buffer> skinCb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transparentIndices;
    std::size_t transparentCapacity = 0;
    std::vector<TransparentDraw> transparent;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> doubleSidedRasterizer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11BlendState> weightedBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> fullscreenDepth;
    struct WeightedTargets {
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, 2> targets;
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> views;
        UINT width = 0, height = 0;

        bool ensure(ID3D11Device& device, UINT newWidth, UINT newHeight) {
            if (newWidth == width && newHeight == height) { return views[0] && views[1]; }
            *this = {};
            width = newWidth;
            height = newHeight;
            constexpr DXGI_FORMAT kFormats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16_FLOAT};
            for (std::size_t i = 0; i < std::size(kFormats); ++i) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = width;
                desc.Height = height;
                desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
                desc.Format = kFormats[i];
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                if (FAILED(device.CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
                    FAILED(device.CreateRenderTargetView(texture.Get(), nullptr, targets[i].GetAddressOf())) ||
                    FAILED(device.CreateShaderResourceView(texture.Get(), nullptr, views[i].GetAddressOf()))) {
                    std::cerr << "Cannot allocate weighted transparency targets\n";
                    return false;
                }
            }
            return true;
        }
    };
    WeightedTargets weightedTargets[2];
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> transparentDepth;
    struct OpaqueSnapshot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        UINT width = 0, height = 0, mipLevels = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

        ID3D11ShaderResourceView* capture(ID3D11Device& device, ID3D11DeviceContext& context) {
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
            context.OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
            if (!target) { return nullptr; }

            D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
            target->GetDesc(&targetDesc);
            if (targetDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) { return nullptr; }
            Microsoft::WRL::ComPtr<ID3D11Resource> source;
            target->GetResource(source.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture;
            if (FAILED(source.As(&sourceTexture))) { return nullptr; }
            D3D11_TEXTURE2D_DESC desc{};
            sourceTexture->GetDesc(&desc);
            const UINT sourceMip = targetDesc.Texture2D.MipSlice;
            desc.Width = std::max(1u, desc.Width >> sourceMip);
            desc.Height = std::max(1u, desc.Height >> sourceMip);
            desc.Format = targetDesc.Format;

            desc.MipLevels = static_cast<UINT>(std::bit_width(std::max(desc.Width, desc.Height)));
            if (desc.Width != width || desc.Height != height || desc.Format != format || desc.MipLevels != mipLevels) {
                width = desc.Width;
                height = desc.Height;
                format = desc.Format;
                texture.Reset();
                view.Reset();
                mipLevels = desc.MipLevels;
                desc.ArraySize = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags = 0;
                if (mipLevels > 1) {
                    desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
                    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                }
                desc.CPUAccessFlags = 0;
                if (FAILED(device.CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
                    FAILED(device.CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
                    texture.Reset();
                    view.Reset();
                    std::cerr << "Cannot allocate opaque scene texture for glass\n";
                    return nullptr;
                }
            }
            if (!view) { return nullptr; }

            // Keep the sample source separate from the active render target.
            context.OMSetRenderTargets(0, nullptr, nullptr);
            context.CopySubresourceRegion(texture.Get(), 0, 0, 0, 0, sourceTexture.Get(), sourceMip, nullptr);
            if (mipLevels > 1) { context.GenerateMips(view.Get()); }
            auto* color = target.Get();
            context.OMSetRenderTargets(1, &color, depth.Get());
            return view.Get();
        }
    };
    OpaqueSnapshot opaqueSnapshots[2];
    CascadedShadowMap shadowMap;
    CascadedShadowMap spotMap;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> environmentSampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> transmissionSampler;
    GpuMesh cubeMesh{};
    ID3D11Buffer* lightsCb = nullptr;
    ID3D11Buffer* clusterCb = nullptr;
    std::vector<const LightInstance*> lightOrder;
    bool shadowAllocationFailed = false;
    bool ready = false;
    bool failed = false;

};

bool MeshPass::bindSkin(ID3D11DeviceContext* context, EntityId id, const GpuMesh& mesh) const {
    if (!m_impl || !m_impl->currentScene || !mesh.skinVertices) { return false; }
    auto* vertices = m_impl->currentScene->skinnedVerticesView();
    const auto base = m_impl->currentScene->skinnedVertexBase(id);
    if (!vertices || !base) { return false; }
    const SkinCb constants{*base};
    context->UpdateSubresource(m_impl->skinCb.Get(), 0, nullptr, &constants, 0, 0);
    context->VSSetConstantBuffers(5, 1, m_impl->skinCb.GetAddressOf());
    context->VSSetShaderResources(kSkinnedVertexSlot, 1, &vertices);
    return true;
}

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

void MeshPass::setAssetReader(AssetReader reader, bool asynchronous) {
    m_assets.setReader(std::move(reader), asynchronous);
    ++m_assetRevision;
}
std::uint64_t MeshPass::sceneDrawRevision(const Scene& scene) {
    if (m_impl) {
        for (auto& cached : m_impl->scenes) {
            if (cached.source() == &scene) { return cached.observeRevision(scene); }
        }
    }
    return 0;
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

    D3D11_BUFFER_DESC skinDesc{};
    skinDesc.ByteWidth = sizeof(SkinCb);
    skinDesc.Usage = D3D11_USAGE_DEFAULT;
    skinDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&skinDesc, nullptr, m_impl->skinCb.GetAddressOf()))) {
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
    raster.CullMode = D3D11_CULL_NONE;
    if (FAILED(hr) || FAILED(device->CreateRasterizerState(&raster, m_impl->doubleSidedRasterizer.GetAddressOf()))) {
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

    D3D11_DEPTH_STENCIL_DESC fullscreenDepth{};
    fullscreenDepth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device->CreateDepthStencilState(&fullscreenDepth, m_impl->fullscreenDepth.GetAddressOf()))) {
        std::cerr << "Cannot create fullscreen depth state\n";
    }
    blend.IndependentBlendEnable = TRUE;
    target.SrcBlend = target.DestBlend = target.SrcBlendAlpha = target.DestBlendAlpha = D3D11_BLEND_ONE;
    auto& reveal = blend.RenderTarget[1];
    reveal = target;
    reveal.SrcBlend = D3D11_BLEND_ZERO;
    reveal.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    reveal.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
    if (FAILED(device->CreateBlendState(&blend, m_impl->weightedBlend.GetAddressOf()))) {
        std::cerr << "Cannot create weighted transparency blend state\n";
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
    environmentSampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device->CreateSamplerState(&environmentSampler, m_impl->transmissionSampler.GetAddressOf()))) {
        std::cerr << "Cannot create scene color sampler for glass\n";
    }

    m_impl->ready = true;
    return true;
}

void MeshPass::draw(
    ID3D11DeviceContext* context, ShaderLibrary& shaders,
    const Mat4& view, const Mat4& projection, Vec3 eye,
    const SceneDrawList& drawList, bool hdr, DVec3 renderOrigin, bool prepareAssets, GameViewStats* stats,
    ID3D11ShaderResourceView* depth, GpuViewStats* gpuStats, std::uint64_t frameNumber, bool sceneView) {
    if (!m_impl || !m_impl->ready || !context ||
        (!drawList.scene && (!drawList.meshes || drawList.meshCount <= 0))) {
        return;
    }
    const auto program = shaders.program(ShaderId::Mesh);
    auto* shadowPixel = shaders.program(ShaderId::Shadow).pixel;

    const LightInstance* spotCaster = nullptr;
    for (int i = 0; drawList.lights && i < drawList.lightCount; ++i) {
        const auto& light = drawList.lights[i];
        if (light.type == LightType::Spot && light.castShadow && light.range > 0.0f &&
            dot(light.radiance, light.radiance) > 0.0f && (!spotCaster || light.entity < spotCaster->entity)) {
            spotCaster = &light;
        }
    }
    const auto syncStart = std::chrono::steady_clock::now();
    const unsigned viewIndex = sceneView ? 0 : 1;
    auto& culling = m_impl->culling[viewIndex];
    // The editor and game view share buffers when both render the same Scene.
    auto& first = m_impl->scenes[viewIndex];
    auto& second = m_impl->scenes[1 - viewIndex];
    auto& scene = !drawList.scene ? m_impl->transientScene :
        (second.source() == drawList.scene ? second : first);
    m_impl->currentScene = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    ID3D11ShaderResourceView* nullView = nullptr;
    context->VSSetShaderResources(kSkinnedVertexSlot, 1, &nullView);
    context->VSSetShaderResources(13, 1, &nullView);
    const auto sync = [&] {
        return scene.sync(*context, shaders, m_assets, m_impl->cubeMesh, drawList,
            m_assetRevision, renderOrigin, prepareAssets, frameNumber);
    };
    const auto syncScene = [&] {
        if (!sync()) { return false; }
        if (drawList.animationOcclusion && drawList.animationOcclusion == drawList.animation &&
            drawList.animationOcclusion->refreshOcclusionPoses(scene.geometryMoved() || spotCaster != nullptr)) {
            return sync();
        }
        return true;
    };
    if (!syncScene() || !culling.ensure(*device.Get(), static_cast<UINT>(scene.batches().size()), scene.itemCount())) {
        if (!m_impl->failed) { std::cerr << "Cannot synchronize GPU mesh scene or allocate culling buffers\n"; }
        m_impl->failed = true;
        scene.retryNextFrame();
        culling.invalidateDepth();
        return;
    }
    m_impl->failed = false;
    m_impl->currentScene = &scene;
    if (stats) {
        stats->cpuSceneSyncMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - syncStart).count();
    }
    const auto submitStart = std::chrono::steady_clock::now();

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
    if (spotCaster && !order.empty() && std::find(order.begin(), order.begin() + lightCount, spotCaster) ==
        order.begin() + lightCount) { order[lightCount - 1] = spotCaster; }
    const auto lightAt = [&](int index) -> const LightInstance& {
        return order.empty() ? drawList.lights[index] : *order[index];
    };
    D3D11_VIEWPORT viewport{};
    UINT viewportCount = 1;
    context->RSGetViewports(&viewportCount, &viewport);
    if (viewportCount == 0 || viewport.Width <= 0 || viewport.Height <= 0) { return; }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    MeshFrameCb frame{};
    splitOrigin(renderOrigin, frame.originHigh, frame.originLow);
    auto* environment = m_assets.environment(drawList.environmentPath);
    frame.environmentIntensity = environment && m_impl->environmentSampler ? drawList.environmentIntensity : 0.0f;
    const auto sky = shaders.program(ShaderId::EnvironmentSky);
    if (drawList.showEnvironmentSky && environment && m_impl->environmentSampler &&
        sky.vertex && sky.pixel && m_impl->fullscreenDepth &&
        SUCCEEDED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        Mat4 rotation = view;
        rotation.m[12] = rotation.m[13] = rotation.m[14] = 0;
        const Mat4 skyProjection = projection.m[15] == 1.0f ?
            perspective(90.0f, viewport.Width / viewport.Height, 0.1f, 10.0f) : projection;
        const Mat4 worldFromClip = inverse(skyProjection * rotation);
        auto* constants = static_cast<MeshFrameCb*>(mapped.pData);
        std::memcpy(constants->viewProj, worldFromClip.m, sizeof(worldFromClip.m));
        constants->eye[0] = frame.environmentIntensity;
        constants->eye[1] = hdr ? 1.0f : 0.0f;
        context->Unmap(m_impl->frameCb, 0);
        context->PSSetConstantBuffers(0, 1, &m_impl->frameCb);
        context->PSSetShaderResources(4, 1, &environment);
        context->PSSetSamplers(1, 1, m_impl->environmentSampler.GetAddressOf());
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        drawFullscreen(*context, sky, m_impl->doubleSidedRasterizer.Get(), m_impl->fullscreenDepth.Get());
        context->PSSetShaderResources(4, 1, &nullView);
    }
    if (scene.batches().empty() && scene.transparent().empty()) { culling.invalidateDepth(); return; }

    frame.shadowLight = -1;
    frame.spotLight = -1;
    auto& spotMap = m_impl->spotMap;
    auto& shadowMap = m_impl->shadowMap;
    if (drawList.directionalShadows) {
        for (int i = 0; i < lightCount; ++i) {
            const auto& light = lightAt(i);
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
    if (spotCaster) {
        for (int i = 0; i < lightCount; ++i) {
            if (&lightAt(i) == spotCaster && spotMap.updateSpot(context, spotCaster->position, spotCaster->direction,
                spotCaster->outerCos, spotCaster->range)) {
                frame.spotLight = i;
                const auto& spotViewProj = spotMap.cascade(0).viewProj;
                std::memcpy(frame.spotViewProj, spotViewProj.m, sizeof(spotViewProj.m));
                break;
            }
        }
    }
    const unsigned shadowSlots = std::max(frame.cascadeCount, frame.spotLight >= 0 ? 1u : 0u);
    if (shadowSlots != 0 && !culling.ensureShadows(*device.Get(), static_cast<UINT>(scene.batches().size()),
            scene.itemCount(), shadowSlots)) {
        if (!m_impl->shadowAllocationFailed) { std::cerr << "Cannot allocate shadow culling buffers\n"; }
        m_impl->shadowAllocationFailed = true;
        frame.spotLight = -1;
        frame.cascadeCount = 0;
        frame.shadowLight = -1;
    } else {
        m_impl->shadowAllocationFailed = false;
    }
    std::memcpy(frame.viewProj, viewProj.m, sizeof(frame.viewProj));
    frame.orthographic = projection.m[15] == 1.0f;
    frame.orthoViewToEye[0] = -view.m[2];
    frame.orthoViewToEye[1] = -view.m[6];
    frame.orthoViewToEye[2] = -view.m[10];
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
            const auto& light = lightAt(i);
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
        const auto& light = lightAt(i);
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
    auto* boundRasterizer = m_impl->rasterizer;
    context->RSSetState(boundRasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);

    const GpuMesh* boundMesh = nullptr;
    const GpuMaterial* boundMaterial = nullptr;
    const CascadedShadowMap* shadowPass = nullptr;
    ID3D11PixelShader* weightedPixel = nullptr;
    bool passChanged = true;
    ID3D11PixelShader* boundPixel = program.pixel;
    auto* boundVertex = program.vertex;
    auto* boundLayout = program.layout;
    auto* instances = scene.instancesView();
    auto* skinnedVertices = scene.skinnedVerticesView();
    context->VSSetShaderResources(13, 1, &instances);
    context->VSSetShaderResources(kSkinnedVertexSlot, 1, &skinnedVertices);
    const auto skinned = shaders.program(ShaderId::MeshSkinned);
    const auto bindDraw = [&](const GpuScene::DrawRange& draw) {
        const auto vertexProgram = draw.mesh->skinVertices ? skinned : program;
        if (vertexProgram.vertex != boundVertex) {
            context->VSSetShader(vertexProgram.vertex, nullptr, 0);
            boundVertex = vertexProgram.vertex;
            passChanged = true;
        }
        if (vertexProgram.layout != boundLayout) {
            context->IASetInputLayout(vertexProgram.layout);
            boundLayout = vertexProgram.layout;
        }
        const auto* mesh = draw.mesh;
        const auto* material = draw.material;
        auto* rasterizer = shadowPass ? shadowPass->rasterizer(material->doubleSided) :
            (material->doubleSided ? m_impl->doubleSidedRasterizer.Get() : m_impl->rasterizer);
        if (rasterizer != boundRasterizer) {
            context->RSSetState(rasterizer);
            boundRasterizer = rasterizer;
            passChanged = true;
        }
        if (mesh != boundMesh) {
            const UINT offset = 0;
            if (!boundMesh || mesh->vertexBuffer != boundMesh->vertexBuffer) {
                context->IASetVertexBuffers(0, 1, &mesh->vertexBuffer, &mesh->vertexStride, &offset);
            }
            if (!boundMesh || mesh->indexBuffer != boundMesh->indexBuffer || mesh->indexFormat != boundMesh->indexFormat) {
                context->IASetIndexBuffer(mesh->indexBuffer, mesh->indexFormat, 0);
            }
            boundMesh = mesh;
        }
        if (shadowPass || material != boundMaterial) {
            auto* custom = shadowPass ? nullptr : scene.customPixel(material);
            auto* pixel = shadowPass ? (material->alphaMode == AlphaMode::Mask ? shadowPixel : nullptr)
                                     : (weightedPixel ? weightedPixel : (custom ? custom : program.pixel));
            if (pixel != boundPixel) {
                context->PSSetShader(pixel, nullptr, 0);
                boundPixel = pixel;
                passChanged = true;
            }
        }
        if (material != boundMaterial && (!shadowPass || material->alphaMode == AlphaMode::Mask)) {
            context->PSSetConstantBuffers(2, 1, &material->constants);
            if (!boundMaterial || material->samplers[0] != boundMaterial->samplers[0]) {
                context->PSSetSamplers(0, 1, material->samplers);
            }
            const auto extraSamplerCount = static_cast<UINT>(std::size(material->samplers) - 1);
            if (!shadowPass && (!boundMaterial ||
                !std::equal(material->samplers + 1, material->samplers + 1 + extraSamplerCount, boundMaterial->samplers + 1))) {
                context->PSSetSamplers(3, extraSamplerCount, material->samplers + 1);
            }
            if (!boundMaterial || !std::equal(material->views, material->views + 4, boundMaterial->views)) {
                context->PSSetShaderResources(0, 4, material->views);
            }
            const auto extraViewCount = static_cast<UINT>(std::size(material->extraViews));
            if (!shadowPass && (!boundMaterial ||
                !std::equal(material->extraViews, material->extraViews + extraViewCount, boundMaterial->extraViews))) {
                context->PSSetShaderResources(6, extraViewCount, material->extraViews);
            }
            boundMaterial = material;
            passChanged = true;
        }
    };
    const auto countDraw = [&] {
        if (stats) {
            ++stats->drawCalls;
            stats->setPassCalls += passChanged;
        }
        passChanged = false;
    };
    const auto submit = [&](MeshQueue queue, GpuCulling::Phase phase, const FrustumPlanes& planes,
        bool useDepth = false, unsigned cascade = 0) {
        auto* args = culling.cull(*context, shaders, scene, queue, phase, planes, renderOrigin, useDepth, cascade);
        if (!args) { return; }
        UINT offset = 0;
        for (const auto& batch : scene.batches()) {
            if ((batch.passes & meshQueueMask(queue)) != 0 &&
                (!shadowPass || batch.material->alphaMode != AlphaMode::Mask || shadowPixel)) {
                bindDraw(batch);
                context->DrawIndexedInstancedIndirect(args, offset);
                countDraw();
            }
            offset += static_cast<UINT>(sizeof(GpuBatch));
        }
        if (gpuStats) { gpuStats->capture(*device.Get(), *context, scene, args, queue, shadowPixel != nullptr); }
    };
    if (gpuStats) { gpuStats->mark(*context, ViewTimestamp::ShadowStart); }

    if (frame.cascadeCount != 0 || frame.spotLight >= 0) {
        context->PSSetShaderResources(5, 1, &nullView);
        context->PSSetShaderResources(kSpotShadowSlot, 1, &nullView);
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
        context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
        const auto drawShadow = [&](CascadedShadowMap& map, unsigned slot, const FrustumPlanes& planes) {
            if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return false; }
            MeshFrameCb shadowFrame = frame;
            const auto& lightViewProj = map.cascade(slot).viewProj;
            std::memcpy(shadowFrame.viewProj, lightViewProj.m, sizeof(lightViewProj.m));
            std::memcpy(mapped.pData, &shadowFrame, sizeof(shadowFrame));
            context->Unmap(m_impl->frameCb, 0);

            const float size = static_cast<float>(map.resolution());
            const D3D11_VIEWPORT shadowViewport{0, 0, size, size, 0, 1};
            context->RSSetViewports(1, &shadowViewport);
            auto* depthView = map.depthView(slot);
            context->OMSetRenderTargets(0, nullptr, depthView);
            context->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, 1, 0);
            shadowPass = &map;
            passChanged = true;
            submit(MeshQueue::Shadow, GpuCulling::Phase::Frustum, planes, false, slot);
            return true;
        };
        for (unsigned cascade = 0; cascade < frame.cascadeCount; ++cascade) {
            if (!drawShadow(shadowMap, cascade, shadowMap.cullingPlanes(cascade))) {
                frame.shadowLight = -1;
                frame.cascadeCount = 0;
                break;
            }
        }
        if (frame.spotLight >= 0 && !drawShadow(spotMap, 0, frustumPlanes(spotMap.cascade(0).viewProj))) {
            frame.spotLight = -1;
        }
        auto* target = previousTarget.Get();
        context->OMSetRenderTargets(1, &target, previousDepth.Get());
        context->RSSetViewports(1, &viewport);
        boundRasterizer = m_impl->rasterizer;
        context->RSSetState(boundRasterizer);
        context->PSSetShader(program.pixel, nullptr, 0);
        boundPixel = program.pixel;
        shadowPass = nullptr;
        passChanged = true;
        boundMaterial = nullptr;
    }
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
    std::memcpy(mapped.pData, &frame, sizeof(frame));
    context->Unmap(m_impl->frameCb, 0);

    if (gpuStats) { gpuStats->mark(*context, ViewTimestamp::ShadowEnd); }
    ID3D11ShaderResourceView* lightingViews[] = {environment, frame.cascadeCount != 0 ? shadowMap.shaderView() : nullptr};
    ID3D11SamplerState* lightingSamplers[] = {m_impl->environmentSampler.Get(),
        frame.spotLight >= 0 ? spotMap.sampler() : shadowMap.sampler()};
    auto* spotView = frame.spotLight >= 0 ? spotMap.shaderView() : nullptr;
    context->PSSetShaderResources(kSpotShadowSlot, 1, &spotView);
    context->PSSetShaderResources(4, 2, lightingViews);
    context->PSSetSamplers(1, 2, lightingSamplers);
    const auto planes = frustumPlanes(viewProj);
    const auto width = static_cast<UINT>(viewport.Width);
    const auto height = static_cast<UINT>(viewport.Height);
    if (scene.itemRange(MeshQueue::Opaque).count != 0) {
        // The previous pyramid is only a hint. Every history reject is retested
        // against current-frame opaque depth, including after a large camera move.
        const bool hasHistory = culling.hasDepth(depth, width, height);
        submit(MeshQueue::Opaque, GpuCulling::Phase::FirstOpaque, planes, hasHistory);
        if (depth) {
            const bool built = culling.buildDepth(*device.Get(), *context, shaders, depth, width, height, viewProj, renderOrigin);
            if (hasHistory) {
                submit(MeshQueue::Opaque, GpuCulling::Phase::SecondOpaque, planes, built);
                if (built) {
                    (void)culling.buildDepth(*device.Get(), *context, shaders, depth, width, height, viewProj, renderOrigin);
                }
            }
            if (drawList.animationOcclusion && drawList.animationOcclusion == drawList.animation && built && !spotCaster) {
                culling.sampleAnimationOcclusion(*device.Get(), *context, shaders, *drawList.animationOcclusion);
            }
        }
    } else {
        culling.invalidateDepth();
    }
    if (gpuStats) { gpuStats->mark(*context, ViewTimestamp::OpaqueEnd); }
    auto& transparent = m_impl->transparent;
    transparent.clear();
    const bool hasTransmission = scene.itemRange(MeshQueue::Transmission).count != 0;
    bool needsSnapshot = hasTransmission;
    const auto composite = shaders.program(ShaderId::TransparencyComposite);
    auto* weightedShader = shaders.program(ShaderId::MeshWeighted).pixel;
    bool weighted = drawList.weightedTransparency && weightedShader && composite.vertex && composite.pixel &&
        m_impl->weightedBlend && m_impl->fullscreenDepth && viewport.TopLeftX == 0 && viewport.TopLeftY == 0;
    for (const auto& item : scene.transparent()) {
        const auto& instance = scene.instance(item.instance);
        const Vec3 translation = relativePosition(instance, renderOrigin);
        const auto& bounds = instance.bounds;
        if (!insideFrustum(planes, translation + Vec3{bounds[0], bounds[1], bounds[2]}, bounds[3])) { continue; }
        weighted &= item.range.material->shaderPath.empty();
        const auto& basis = instance.basis;
        const Vec3 center = translation + Vec3{
            basis[0][0] * item.localCenter.x + basis[1][0] * item.localCenter.y + basis[2][0] * item.localCenter.z,
            basis[0][1] * item.localCenter.x + basis[1][1] * item.localCenter.y + basis[2][1] * item.localCenter.z,
            basis[0][2] * item.localCenter.x + basis[1][2] * item.localCenter.y + basis[2][2] * item.localCenter.z};
        const float z = view.m[2] * center.x + view.m[6] * center.y + view.m[10] * center.z + view.m[14];
        transparent.push_back({&item, std::isfinite(z) ? z : 0.0f});
        needsSnapshot |= item.range.material->transmissive;
    }
    if (needsSnapshot) {
        auto& snapshot = m_impl->opaqueSnapshots[viewIndex];
        auto* background = m_impl->transmissionSampler ? snapshot.capture(*device.Get(), *context) : nullptr;
        context->PSSetShaderResources(kOpaqueSceneSlot, 1, &background);
        context->PSSetSamplers(kOpaqueSceneSlot, 1, m_impl->transmissionSampler.GetAddressOf());
        if (hasTransmission) { submit(MeshQueue::Transmission, GpuCulling::Phase::Frustum, planes); }
    }
    if (gpuStats) { gpuStats->mark(*context, ViewTimestamp::TransmissionEnd); }
    if (!transparent.empty()) {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorTarget;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> sceneDepth;
        auto& targets = m_impl->weightedTargets[viewIndex];
        if (weighted) {
            context->OMGetRenderTargets(1, colorTarget.GetAddressOf(), sceneDepth.GetAddressOf());
            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            if (colorTarget) { colorTarget->GetDesc(&desc); }
            weighted = desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D && targets.ensure(*device.Get(), width, height);
        }
        if (!weighted) {
            std::sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) {
                if (a.depth != b.depth) { return a.depth > b.depth; }
                return a.draw->instance < b.draw->instance;
            });
        }
        if (transparent.size() > m_impl->transparentCapacity) {
            const auto capacity = std::bit_ceil(transparent.size());
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(capacity * sizeof(unsigned));
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Microsoft::WRL::ComPtr<ID3D11Buffer> candidate;
            if (SUCCEEDED(device->CreateBuffer(&desc, nullptr, candidate.GetAddressOf()))) {
                m_impl->transparentIndices = std::move(candidate);
                m_impl->transparentCapacity = capacity;
            } else {
                std::cerr << "Cannot allocate transparent instance indices\n";
            }
        }
        if (transparent.size() <= m_impl->transparentCapacity &&
            SUCCEEDED(context->Map(m_impl->transparentIndices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            auto* indices = static_cast<unsigned*>(mapped.pData);
            for (const auto& entry : transparent) { *indices++ = entry.draw->instance; }
            context->Unmap(m_impl->transparentIndices.Get(), 0);
            const UINT stride = sizeof(unsigned), offset = 0;
            context->IASetVertexBuffers(1, 1, m_impl->transparentIndices.GetAddressOf(), &stride, &offset);
            if (weighted) {
                const float zero[4]{};
                const float one[4]{1, 1, 1, 1};
                ID3D11RenderTargetView* drawTargets[] = {targets.targets[0].Get(), targets.targets[1].Get()};
                context->ClearRenderTargetView(drawTargets[0], zero);
                context->ClearRenderTargetView(drawTargets[1], one);
                context->OMSetRenderTargets(2, drawTargets, sceneDepth.Get());
                context->OMSetBlendState(m_impl->weightedBlend.Get(), nullptr, 0xffffffff);
                weightedPixel = weightedShader;
                boundMaterial = nullptr;
            } else {
                context->OMSetBlendState(m_impl->blend.Get(), nullptr, 0xffffffff);
            }
            context->OMSetDepthStencilState(m_impl->transparentDepth.Get(), 0);
            passChanged = true;
            unsigned firstInstance = 0;
            for (const auto& entry : transparent) {
                const auto& draw = entry.draw->range;
                bindDraw(draw);
                context->DrawIndexedInstanced(draw.indexCount, 1, draw.mesh->firstIndex + draw.firstIndex,
                    draw.mesh->baseVertex, firstInstance++);
                countDraw();
                if (stats) {
                    stats->triangles += draw.indexCount / 3;
                    stats->vertices += draw.vertexCount;
                }
            }
            if (weighted) {
                auto* color = colorTarget.Get();
                context->OMSetRenderTargets(1, &color, nullptr);
                ID3D11ShaderResourceView* inputs[] = {targets.views[0].Get(), targets.views[1].Get()};
                context->PSSetShaderResources(0, 2, inputs);
                context->OMSetBlendState(m_impl->blend.Get(), nullptr, 0xffffffff);
                drawFullscreen(*context, composite, m_impl->doubleSidedRasterizer.Get(), m_impl->fullscreenDepth.Get());
                if (stats) { ++stats->drawCalls; ++stats->setPassCalls; }
                context->PSSetShaderResources(0, 1, &nullView);
                context->PSSetShaderResources(1, 1, &nullView);
                context->OMSetRenderTargets(1, &color, sceneDepth.Get());
                context->RSSetState(m_impl->rasterizer);
            }
            context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        }
    }
    if (gpuStats) { gpuStats->mark(*context, ViewTimestamp::TransparentEnd); }
    if (stats) {
        stats->cpuMeshSubmitMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - submitStart).count();
    }
    context->VSSetShaderResources(13, 1, &nullView);
    if (boundRasterizer != m_impl->rasterizer) { context->RSSetState(m_impl->rasterizer); }
    ID3D11ShaderResourceView* unbound[kOpaqueSceneSlot + 1]{};
    context->PSSetShaderResources(0, static_cast<UINT>(std::size(unbound)), unbound);
    context->PSSetShaderResources(kSpotShadowSlot, 1, &nullView);
}

} // namespace sat
