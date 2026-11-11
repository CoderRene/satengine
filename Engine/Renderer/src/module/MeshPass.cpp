#include "MeshPass.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <d3dcompiler.h>

#include "AssetData.hpp"

#include <cstring>
#include <utility>
#include <iostream>

namespace sat {

namespace {

static_assert(sizeof(MeshVertex) == 5 * sizeof(float));

template <typename T>
void releaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}


struct MeshFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
};

struct MeshObjectCb {
    float world[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

constexpr const char* kMeshShaderSource = R"(
cbuffer Frame : register(b0)
{
    float4x4 viewProj;
};

cbuffer Object : register(b1)
{
    float4x4 world;
    float4 color;
};

struct VSIn
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

VSOut vs_main(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    output.position = mul(viewProj, worldPos);
    output.color = color;
    output.uv = input.uv;
    return output;
}

Texture2D albedo : register(t0);
SamplerState albedoSampler : register(s0);

float4 ps_main(VSOut input) : SV_Target
{
    const float4 sampleColor = albedo.Sample(albedoSampler, input.uv);
    return float4(input.color.rgb * sampleColor.rgb, input.color.a);
}
)";

void fillUnitCube(MeshVertex* vertices, unsigned short* indices) {
    const float p[8][3] = {
        {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,0.5f,0.5f}, {-0.5f,0.5f,0.5f}
    };
    const int faces[6][4] = {{0,1,2,3}, {4,7,6,5}, {0,3,7,4}, {1,5,6,2}, {0,4,5,1}, {3,2,6,7}};
    const float uv[4][2] = {{0,1}, {1,1}, {1,0}, {0,0}};
    for (int face = 0; face < 6; ++face) {
        for (int corner = 0; corner < 4; ++corner) {
            const auto* position = p[faces[face][corner]];
            vertices[face * 4 + corner] = {position[0], position[1], position[2], uv[corner][0], uv[corner][1]};
        }
        const auto base = static_cast<unsigned short>(face * 4);
        const unsigned short triangle[6] = {base, static_cast<unsigned short>(base + 1), static_cast<unsigned short>(base + 2),
            base, static_cast<unsigned short>(base + 2), static_cast<unsigned short>(base + 3)};
        for (int i = 0; i < 6; ++i) { indices[face * 6 + i] = triangle[i]; }
    }
}

} // namespace

struct MeshPass::Impl {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11Buffer* objectCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    GpuMesh cubeMesh{};
    ID3D11ShaderResourceView* whiteTexture = nullptr;
    ID3D11SamplerState* sampler = nullptr;
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
    releaseCom(m_impl->whiteTexture);
    releaseCom(m_impl->sampler);
    releaseCom(m_impl->depthState);
    releaseCom(m_impl->rasterizer);
    releaseCom(m_impl->objectCb);
    releaseCom(m_impl->frameCb);
    releaseCom(m_impl->ib);
    releaseCom(m_impl->vb);
    releaseCom(m_impl->layout);
    releaseCom(m_impl->ps);
    releaseCom(m_impl->vs);
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

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(
        kMeshShaderSource,
        std::strlen(kMeshShaderSource),
        "MeshPass",
        nullptr,
        nullptr,
        "vs_main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile mesh VS";
        if (errorBlob != nullptr) {
            std::cerr << ": " << static_cast<const char*>(errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        std::cerr << '\n';
        m_impl->failed = true;
        return false;
    }
    releaseCom(errorBlob);

    hr = D3DCompile(
        kMeshShaderSource,
        std::strlen(kMeshShaderSource),
        "MeshPass",
        nullptr,
        nullptr,
        "ps_main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile mesh PS";
        if (errorBlob != nullptr) {
            std::cerr << ": " << static_cast<const char*>(errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        std::cerr << '\n';
        releaseCom(vsBlob);
        m_impl->failed = true;
        return false;
    }
    releaseCom(errorBlob);

    hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_impl->vs);
    if (FAILED(hr)) {
        releaseCom(vsBlob);
        releaseCom(psBlob);
        m_impl->failed = true;
        return false;
    }

    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_impl->ps);
    releaseCom(psBlob);
    if (FAILED(hr)) {
        releaseCom(vsBlob);
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device->CreateInputLayout(
        layoutDesc,
        2,
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_impl->layout);
    releaseCom(vsBlob);
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }

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

    const unsigned char white[4] = {255, 255, 255, 255};
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = textureDesc.Height = textureDesc.MipLevels = textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA textureData{};
    textureData.pSysMem = white;
    textureData.SysMemPitch = 4;
    ID3D11Texture2D* texture = nullptr;
    hr = device->CreateTexture2D(&textureDesc, &textureData, &texture);
    if (SUCCEEDED(hr)) {
        hr = device->CreateShaderResourceView(texture, nullptr, &m_impl->whiteTexture);
    }
    releaseCom(texture);
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) { hr = device->CreateSamplerState(&sampler, &m_impl->sampler); }
    if (FAILED(hr)) {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    }
    m_impl->ready = true;
    return true;
}

void MeshPass::draw(
    ID3D11DeviceContext* context,
    const Mat4& viewProj,
    const SceneDrawList& drawList) const {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr || drawList.meshes == nullptr ||
        drawList.meshCount <= 0) {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* frame = static_cast<MeshFrameCb*>(mapped.pData);
    std::memcpy(frame->viewProj, viewProj.m, sizeof(frame->viewProj));
    context->Unmap(m_impl->frameCb, 0);

    context->IASetInputLayout(m_impl->layout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_impl->vs, nullptr, 0);
    context->PSSetShader(m_impl->ps, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->VSSetConstantBuffers(1, 1, &m_impl->objectCb);
    context->PSSetConstantBuffers(1, 1, &m_impl->objectCb);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->PSSetSamplers(0, 1, &m_impl->sampler);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);

    const GpuMesh* boundMesh = nullptr;
    for (int i = 0; i < drawList.meshCount; ++i) {
        const MeshInstance& instance = drawList.meshes[i];
        const GpuMesh* mesh = geometry(instance);
        if (mesh == nullptr || !mesh->valid()) {
            continue;
        }
        if (mesh != boundMesh) {
            const UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &mesh->vertexBuffer, &mesh->vertexStride, &offset);
            context->IASetIndexBuffer(mesh->indexBuffer, mesh->indexFormat, 0);
            boundMesh = mesh;
        }
        if (FAILED(context->Map(m_impl->objectCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            continue;
        }
        auto* object = static_cast<MeshObjectCb*>(mapped.pData);
        std::memcpy(object->world, instance.world.m, sizeof(object->world));
        std::memcpy(object->color, instance.color, sizeof(object->color));
        context->Unmap(m_impl->objectCb, 0);
        auto* texture = instance.texturePath.empty() ? nullptr : m_assets.texture(instance.texturePath);
        if (texture == nullptr) { texture = m_impl->whiteTexture; }
        auto* sampler = instance.texturePath.empty() ? nullptr : m_assets.sampler(instance.texturePath);
        if (sampler == nullptr) { sampler = m_impl->sampler; }
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetShaderResources(0, 1, &texture);
        context->DrawIndexed(mesh->indexCount, 0, 0);
    }
    ID3D11ShaderResourceView* unbound = nullptr;
    context->PSSetShaderResources(0, 1, &unbound);
}

} // namespace sat
