#include "MeshPass.hpp"

#include "ShaderLayout.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>

#include "AssetData.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <utility>
#include <iostream>

namespace sat {

namespace {

static_assert(sizeof(MeshVertex) == 12 * sizeof(float));

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
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11Buffer* objectCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> transparentDepth;
    std::vector<const MeshInstance*> transparent;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> shadowDepth;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadowView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> shadowSampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> environmentSampler;
    UINT shadowResolutionAttempted = 0;
    UINT shadowResolution = 0;
    GpuMesh cubeMesh{};
    ID3D11Buffer* lightsCb = nullptr;
    ID3D11Buffer* clusterCb = nullptr;
    std::vector<const LightInstance*> lightOrder;
    bool ready = false;
    bool failed = false;
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
    releaseCom(m_impl->objectCb);
    releaseCom(m_impl->frameCb);
    releaseCom(m_impl->ib);
    releaseCom(m_impl->vb);
    delete m_impl;
    m_impl = nullptr;
}

void MeshPass::setAssetReader(AssetReader reader) { m_assets.setReader(std::move(reader)); }
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
    m_impl->cubeMesh.indexCount = 36;
    m_impl->cubeMesh.vertexStride = sizeof(MeshVertex);
    m_impl->cubeMesh.indexFormat = DXGI_FORMAT_R16_UINT;

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

    cbDesc.ByteWidth = sizeof(MeshObjectCb);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_impl->objectCb);
    if (FAILED(hr)) {
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
    const Mat4& viewProj, Vec3 eye,
    const SceneDrawList& drawList, bool hdr, ID3D11PixelShader* shadowPixel, ShaderLibrary* shaders) const {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr || drawList.meshes == nullptr ||
        drawList.meshCount <= 0) {
        return;
    }

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
    frame.lightingPadding[0] = frame.lightingPadding[1] = 0;
    Mat4 shadowMatrix;
    int shadowIndex = -1;
    const auto requestedShadowSize = static_cast<UINT>(std::clamp(drawList.shadowResolution, 512, 4096));
    if (shadowPixel && drawList.directionalShadows) {
        for (int i = 0; i < lightCount; ++i) {
            const auto& light = order.empty() ? drawList.lights[i] : *order[i];
            if (light.type != LightType::Directional || dot(light.radiance, light.radiance) == 0) { continue; }
            if (m_impl->shadowResolutionAttempted != requestedShadowSize) {
                m_impl->shadowResolutionAttempted = requestedShadowSize;
                Microsoft::WRL::ComPtr<ID3D11Device> device;
                context->GetDevice(device.GetAddressOf());
                D3D11_TEXTURE2D_DESC texture{};
                texture.Width = texture.Height = requestedShadowSize;
                texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
                texture.Format = DXGI_FORMAT_R32_TYPELESS;
                texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
                D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
                dsv.Format = DXGI_FORMAT_D32_FLOAT;
                dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = DXGI_FORMAT_R32_FLOAT;
                srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv.Texture2D.MipLevels = 1;
                D3D11_SAMPLER_DESC sampler{};
                sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
                sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
                sampler.MaxAnisotropy = 1;
                sampler.MaxLOD = D3D11_FLOAT32_MAX;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
                Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
                Microsoft::WRL::ComPtr<ID3D11SamplerState> comparisonSampler;
                if (FAILED(device->CreateTexture2D(&texture, nullptr, depth.GetAddressOf())) ||
                    FAILED(device->CreateDepthStencilView(depth.Get(), &dsv, depthView.GetAddressOf())) ||
                    FAILED(device->CreateShaderResourceView(depth.Get(), &srv, shaderView.GetAddressOf())) ||
                    FAILED(device->CreateSamplerState(&sampler, comparisonSampler.GetAddressOf()))) {
                    std::cerr << "Cannot create directional shadow map at " << requestedShadowSize << " pixels\n";
                } else {
                    m_impl->shadowDepth = std::move(depthView);
                    m_impl->shadowView = std::move(shaderView);
                    m_impl->shadowSampler = std::move(comparisonSampler);
                    m_impl->shadowResolution = requestedShadowSize;
                }
            }
            if (!m_impl->shadowDepth) { break; }
            const float shadowRadius = std::isfinite(drawList.shadowRadius) ?
                std::clamp(drawList.shadowRadius, 5.0f, 200.0f) : 35.0f;
            const Vec3 up = std::abs(light.direction.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            const Vec3 forward = normalize(Vec3{viewProj.m[3], viewProj.m[7], viewProj.m[11]});
            const Vec3 center = eye + forward * (shadowRadius * 0.5f);
            Mat4 lightView = lookAt(center - light.direction * shadowRadius, center, up);
            const float texel = 2 * shadowRadius / static_cast<float>(m_impl->shadowResolution);
            lightView.m[12] = std::round(lightView.m[12] / texel) * texel;
            lightView.m[13] = std::round(lightView.m[13] / texel) * texel;
            Mat4 orthographic;
            orthographic.m[0] = orthographic.m[5] = 1 / shadowRadius;
            orthographic.m[10] = 1 / (2 * shadowRadius);
            shadowMatrix = orthographic * lightView;
            shadowIndex = i;
            break;
        }
    }
    frame.shadowLight = shadowIndex;
    std::memcpy(frame.shadowViewProj, shadowMatrix.m, sizeof(frame.shadowViewProj));
    std::memcpy(frame.viewProj, viewProj.m, sizeof(frame.viewProj));
    frame.eye[0] = eye.x; frame.eye[1] = eye.y; frame.eye[2] = eye.z;
    frame.viewport[0] = viewport.TopLeftX;
    frame.viewport[1] = viewport.TopLeftY;
    frame.viewport[2] = kClusterTiles / viewport.Width;
    frame.viewport[3] = kClusterTiles / viewport.Height;
    frame.lightCount = static_cast<unsigned>(lightCount);
    frame.ambient[0] = drawList.ambient.x; frame.ambient[1] = drawList.ambient.y;
    frame.ambient[2] = drawList.ambient.z; frame.ambient[3] = hdr ? 1.0f : 0.0f;
    MeshFrameCb shadowFrame = frame;
    if (shadowIndex >= 0) { std::memcpy(shadowFrame.viewProj, shadowMatrix.m, sizeof(shadowFrame.viewProj)); }
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
    std::memcpy(mapped.pData, &shadowFrame, sizeof(shadowFrame));
    context->Unmap(m_impl->frameCb, 0);

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
    context->VSSetConstantBuffers(1, 1, &m_impl->objectCb);
    context->PSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->PSSetConstantBuffers(3, 1, &m_impl->lightsCb);
    context->PSSetConstantBuffers(4, 1, &m_impl->clusterCb);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);

    const GpuMesh* boundMesh = nullptr;
    const GpuMaterial* boundMaterial = nullptr;
    bool shadowPass = false;
    ID3D11PixelShader* boundPixel = program.pixel;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (shaders) { context->GetDevice(device.GetAddressOf()); }
    const auto drawMesh = [&](const MeshInstance& instance, const GpuMaterial* material) {
        const GpuMesh* mesh = geometry(instance);
        if (!material || mesh == nullptr || !mesh->valid()) {
            return;
        }
        if (mesh != boundMesh) {
            const UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &mesh->vertexBuffer, &mesh->vertexStride, &offset);
            context->IASetIndexBuffer(mesh->indexBuffer, mesh->indexFormat, 0);
            boundMesh = mesh;
        }
        if (FAILED(context->Map(m_impl->objectCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        auto* object = static_cast<MeshObjectCb*>(mapped.pData);
        std::memcpy(object->world, instance.world.m, sizeof(object->world));
        // Orthogonal TRS columns give inverse-transpose via reciprocal squared lengths.
        const auto& world = instance.world.m;
        const Vec3 x{world[0], world[1], world[2]};
        const Vec3 y{world[4], world[5], world[6]};
        const Vec3 z{world[8], world[9], world[10]};
        const float xx = dot(x, x), yy = dot(y, y), zz = dot(z, z);
        const float tolerance = 1e-5f * std::max({xx, yy, zz});
        const bool uniform = std::abs(xx - yy) <= tolerance && std::abs(xx - zz) <= tolerance;
        const float sign = dot(cross(x, y), z) < 0 ? -1.0f : 1.0f;
        const float scales[] = {xx, yy, zz};
        for (int column = 0; column < 3; ++column) {
            const float scale = uniform || scales[column] <= 1e-20f ? 1.0f : 1.0f / scales[column];
            for (int row = 0; row < 3; ++row) {
                object->normal[column * 4 + row] = world[column * 4 + row] * scale;
            }
            object->normal[column * 4 + 3] = 0;
        }
        object->normal[12] = object->normal[13] = object->normal[14] = 0;
        object->normal[15] = sign;
        context->Unmap(m_impl->objectCb, 0);
        if (!shadowPass && material != boundMaterial) {
            auto* pixel = program.pixel;
            if (shaders && !material->shaderPath.empty()) {
                if (auto* custom = shaders->projectPixel(device.Get(), material->shaderPath)) { pixel = custom; }
            }
            if (pixel != boundPixel) {
                context->PSSetShader(pixel, nullptr, 0);
                boundPixel = pixel;
            }
        }
        if (material != boundMaterial) {
            context->PSSetConstantBuffers(2, 1, &material->constants);
            context->PSSetSamplers(0, 1, &material->sampler);
            context->PSSetShaderResources(0, 4, material->views);
            boundMaterial = material;
        }
        context->DrawIndexed(mesh->indexCount, 0, 0);
    };
    if (shadowIndex >= 0) {
        shadowPass = true;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
        context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
        const D3D11_VIEWPORT shadowViewport{0, 0, static_cast<float>(m_impl->shadowResolution),
            static_cast<float>(m_impl->shadowResolution), 0, 1};
        context->RSSetViewports(1, &shadowViewport);
        context->OMSetRenderTargets(0, nullptr, m_impl->shadowDepth.Get());
        context->ClearDepthStencilView(m_impl->shadowDepth.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        for (int i = 0; i < drawList.meshCount; ++i) {
            const auto& instance = drawList.meshes[i];
            const auto* material = m_assets.material(instance.materialPath);
            if (!material || material->alphaMode == AlphaMode::Blend) { continue; }
            context->PSSetShader(material->alphaMode == AlphaMode::Mask ? shadowPixel : nullptr, nullptr, 0);
            drawMesh(instance, material);
        }
        auto* target = previousTarget.Get();
        context->OMSetRenderTargets(1, &target, previousDepth.Get());
        context->RSSetViewports(1, &viewport);
        context->PSSetShader(program.pixel, nullptr, 0);
        shadowPass = false;
        boundMaterial = nullptr;
        if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
        std::memcpy(mapped.pData, &frame, sizeof(frame));
        context->Unmap(m_impl->frameCb, 0);
    }

    ID3D11ShaderResourceView* lightingViews[] = {environment, shadowIndex >= 0 ? m_impl->shadowView.Get() : nullptr};
    ID3D11SamplerState* lightingSamplers[] = {m_impl->environmentSampler.Get(), m_impl->shadowSampler.Get()};
    context->PSSetShaderResources(4, 2, lightingViews);
    context->PSSetSamplers(1, 2, lightingSamplers);
    auto& transparent = m_impl->transparent;
    transparent.clear();
    for (int i = 0; i < drawList.meshCount; ++i) {
        const auto& instance = drawList.meshes[i];
        const auto* material = m_assets.material(instance.materialPath);
        if (material && material->alphaMode == AlphaMode::Blend) { transparent.push_back(&instance); }
        else { drawMesh(instance, material); }
    }
    if (!transparent.empty()) {
        const auto depthAt = [&](const MeshInstance* mesh) {
            const auto& m = mesh->world.m;
            const float z = viewProj.m[3] * m[12] + viewProj.m[7] * m[13] +
                viewProj.m[11] * m[14] + viewProj.m[15];
            return std::isfinite(z) ? z : 0.0f;
        };
        std::sort(transparent.begin(), transparent.end(), [&](const auto* a, const auto* b) {
            const float aDepth = depthAt(a), bDepth = depthAt(b);
            return aDepth == bDepth ? a < b : aDepth > bDepth;
        });
        context->OMSetBlendState(m_impl->blend.Get(), nullptr, 0xffffffff);
        context->OMSetDepthStencilState(m_impl->transparentDepth.Get(), 0);
        for (const auto* instance : transparent) {
            drawMesh(*instance, m_assets.material(instance->materialPath));
        }
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    }
    ID3D11ShaderResourceView* unbound[6]{};
    context->PSSetShaderResources(0, 6, unbound);
}

} // namespace sat
