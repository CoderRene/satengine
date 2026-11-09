#include "SelectionOutlinePass.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cstring>
#include <iostream>

namespace sat {

namespace {

template <typename T>
void releaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

struct OutlineFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
};

struct OutlineObjectCb {
    float world[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

constexpr float kOutlineScale = 1.06f;
constexpr float kOutlineColor[4] = {1.0f, 0.55f, 0.12f, 1.0f};

constexpr const char* kOutlineShaderSource = R"(
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
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VSOut vs_main(VSIn input)
{
    VSOut output;
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    output.position = mul(viewProj, worldPos);
    output.color = color;
    return output;
}

float4 ps_main(VSOut input) : SV_Target
{
    return input.color;
}
)";

Mat4 scaleUniform(float s) {
    Mat4 out{};
    std::memset(out.m, 0, sizeof(out.m));
    out.m[0] = s;
    out.m[5] = s;
    out.m[10] = s;
    out.m[15] = 1.0f;
    return out;
}

} // namespace

struct SelectionOutlinePass::Impl {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11Buffer* objectCb = nullptr;
    ID3D11RasterizerState* backCull = nullptr;
    ID3D11RasterizerState* frontCull = nullptr;
    ID3D11DepthStencilState* stencilWrite = nullptr;
    ID3D11DepthStencilState* stencilOutline = nullptr;
    ID3D11BlendState* noColorWrite = nullptr;
    bool ready = false;
    bool failed = false;
};

SelectionOutlinePass::~SelectionOutlinePass() {
    release();
}

void SelectionOutlinePass::release() {
    if (m_impl == nullptr) {
        return;
    }
    releaseCom(m_impl->noColorWrite);
    releaseCom(m_impl->stencilOutline);
    releaseCom(m_impl->stencilWrite);
    releaseCom(m_impl->frontCull);
    releaseCom(m_impl->backCull);
    releaseCom(m_impl->objectCb);
    releaseCom(m_impl->frameCb);
    releaseCom(m_impl->layout);
    releaseCom(m_impl->ps);
    releaseCom(m_impl->vs);
    delete m_impl;
    m_impl = nullptr;
}

bool SelectionOutlinePass::ensure(ID3D11Device* device) {
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

    auto fail = [this]() {
        release();
        m_impl = new Impl();
        m_impl->failed = true;
        return false;
    };

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(
        kOutlineShaderSource,
        std::strlen(kOutlineShaderSource),
        "SelectionOutline",
        nullptr,
        nullptr,
        "vs_main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile outline VS";
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
        kOutlineShaderSource,
        std::strlen(kOutlineShaderSource),
        "SelectionOutline",
        nullptr,
        nullptr,
        "ps_main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile outline PS";
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
        return fail();
    }

    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_impl->ps);
    releaseCom(psBlob);
    if (FAILED(hr)) {
        releaseCom(vsBlob);
        return fail();
    }

    const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = device->CreateInputLayout(
        layoutDesc, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_impl->layout);
    releaseCom(vsBlob);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(OutlineFrameCb);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_impl->frameCb);
    if (FAILED(hr)) {
        return fail();
    }
    cbDesc.ByteWidth = sizeof(OutlineObjectCb);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_impl->objectCb);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_BACK;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&raster, &m_impl->backCull);
    if (FAILED(hr)) {
        return fail();
    }
    raster.CullMode = D3D11_CULL_FRONT;
    hr = device->CreateRasterizerState(&raster, &m_impl->frontCull);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_DEPTH_STENCIL_DESC stencilWrite{};
    stencilWrite.DepthEnable = TRUE;
    stencilWrite.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    stencilWrite.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    stencilWrite.StencilEnable = TRUE;
    stencilWrite.StencilReadMask = 0xFF;
    stencilWrite.StencilWriteMask = 0xFF;
    stencilWrite.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencilWrite.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencilWrite.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    stencilWrite.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    stencilWrite.BackFace = stencilWrite.FrontFace;
    hr = device->CreateDepthStencilState(&stencilWrite, &m_impl->stencilWrite);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_DEPTH_STENCIL_DESC stencilOutline{};
    stencilOutline.DepthEnable = FALSE;
    stencilOutline.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    stencilOutline.DepthFunc = D3D11_COMPARISON_ALWAYS;
    stencilOutline.StencilEnable = TRUE;
    stencilOutline.StencilReadMask = 0xFF;
    stencilOutline.StencilWriteMask = 0x00;
    stencilOutline.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencilOutline.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencilOutline.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    stencilOutline.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
    stencilOutline.BackFace = stencilOutline.FrontFace;
    hr = device->CreateDepthStencilState(&stencilOutline, &m_impl->stencilOutline);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = 0;
    hr = device->CreateBlendState(&blend, &m_impl->noColorWrite);
    if (FAILED(hr)) {
        return fail();
    }

    m_impl->ready = true;
    return true;
}

void SelectionOutlinePass::draw(
    ID3D11DeviceContext* context,
    const Mat4& viewProj,
    const MeshInstance& selected,
    const GpuMesh& geometry) const {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr || !geometry.valid()) {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* frame = static_cast<OutlineFrameCb*>(mapped.pData);
    std::memcpy(frame->viewProj, viewProj.m, sizeof(frame->viewProj));
    context->Unmap(m_impl->frameCb, 0);

    const UINT offset = 0;
    context->IASetInputLayout(m_impl->layout);
    context->IASetVertexBuffers(
        0, 1, &geometry.vertexBuffer, &geometry.vertexStride, &offset);
    context->IASetIndexBuffer(geometry.indexBuffer, geometry.indexFormat, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_impl->vs, nullptr, 0);
    context->PSSetShader(m_impl->ps, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->VSSetConstantBuffers(1, 1, &m_impl->objectCb);
    context->PSSetConstantBuffers(1, 1, &m_impl->objectCb);

    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Stencil mask of the selected silhouette using the entity mesh.
    if (FAILED(context->Map(m_impl->objectCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* object = static_cast<OutlineObjectCb*>(mapped.pData);
    std::memcpy(object->world, selected.world.m, sizeof(object->world));
    object->color[0] = 0.0f;
    object->color[1] = 0.0f;
    object->color[2] = 0.0f;
    object->color[3] = 0.0f;
    context->Unmap(m_impl->objectCb, 0);

    context->RSSetState(m_impl->backCull);
    context->OMSetDepthStencilState(m_impl->stencilWrite, 1);
    context->OMSetBlendState(m_impl->noColorWrite, blendFactor, 0xffffffff);
    context->DrawIndexed(geometry.indexCount, 0, 0);

    // Expanded inverted hull where stencil is not marked.
    const Mat4 outlineWorld = selected.world * scaleUniform(kOutlineScale);
    if (FAILED(context->Map(m_impl->objectCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
        return;
    }
    object = static_cast<OutlineObjectCb*>(mapped.pData);
    std::memcpy(object->world, outlineWorld.m, sizeof(object->world));
    std::memcpy(object->color, kOutlineColor, sizeof(object->color));
    context->Unmap(m_impl->objectCb, 0);

    context->RSSetState(m_impl->frontCull);
    context->OMSetDepthStencilState(m_impl->stencilOutline, 1);
    context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    context->DrawIndexed(geometry.indexCount, 0, 0);
    context->OMSetDepthStencilState(nullptr, 0);
}

} // namespace sat
