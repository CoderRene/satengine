#include "TranslateGizmoPass.hpp"

#include "SceneDraw.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace sat {

namespace {

template <typename T>
void releaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

struct GizmoVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct GizmoFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
};

constexpr const char* kGizmoShaderSource = R"(
cbuffer Frame : register(b0)
{
    float4x4 viewProj;
};

struct VSIn
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VSOut vs_main(VSIn input)
{
    VSOut output;
    output.position = mul(viewProj, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}

float4 ps_main(VSOut input) : SV_Target
{
    return input.color;
}
)";

void appendTri(
    std::vector<GizmoVertex>& out,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    float r,
    float g,
    float bCol) {
    out.push_back({a.x, a.y, a.z, r, g, bCol, 1.0f});
    out.push_back({b.x, b.y, b.z, r, g, bCol, 1.0f});
    out.push_back({c.x, c.y, c.z, r, g, bCol, 1.0f});
}

void appendArrow(
    std::vector<GizmoVertex>& out,
    const Vec3& axis,
    float r,
    float g,
    float b) {
    constexpr float kShaftRadius = 0.025f;
    constexpr float kConeRadius = 0.07f;
    constexpr float kConeLength = 0.22f;
    const float shaftLength = kTranslateGizmoAxisLength - kConeLength;
    const Vec3 tip = axis * kTranslateGizmoAxisLength;
    const Vec3 coneBase = axis * shaftLength;

    Vec3 side = {1.0f, 0.0f, 0.0f};
    if (std::fabs(dot(axis, side)) > 0.9f) {
        side = {0.0f, 1.0f, 0.0f};
    }
    const Vec3 u = normalize(cross(axis, side));
    const Vec3 v = normalize(cross(axis, u));

    constexpr int kSegments = 8;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = (static_cast<float>(i) / kSegments) * 6.2831853f;
        const float a1 = (static_cast<float>(i + 1) / kSegments) * 6.2831853f;
        const Vec3 s0 = u * (std::cos(a0) * kShaftRadius) + v * (std::sin(a0) * kShaftRadius);
        const Vec3 s1 = u * (std::cos(a1) * kShaftRadius) + v * (std::sin(a1) * kShaftRadius);
        appendTri(out, s0, coneBase + s0, coneBase + s1, r, g, b);
        appendTri(out, s0, coneBase + s1, s1, r, g, b);

        const Vec3 c0 = coneBase + u * (std::cos(a0) * kConeRadius) +
            v * (std::sin(a0) * kConeRadius);
        const Vec3 c1 = coneBase + u * (std::cos(a1) * kConeRadius) +
            v * (std::sin(a1) * kConeRadius);
        appendTri(out, tip, c0, c1, r, g, b);
        appendTri(out, coneBase, c1, c0, r, g, b);
    }
}

std::vector<GizmoVertex> buildGizmoMesh() {
    std::vector<GizmoVertex> vertices;
    appendArrow(vertices, Vec3{1.0f, 0.0f, 0.0f}, 0.95f, 0.25f, 0.25f);
    appendArrow(vertices, Vec3{0.0f, 1.0f, 0.0f}, 0.25f, 0.90f, 0.30f);
    appendArrow(vertices, Vec3{0.0f, 0.0f, 1.0f}, 0.30f, 0.45f, 0.95f);
    return vertices;
}

} // namespace

struct TranslateGizmoPass::Impl {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    UINT vertexCount = 0;
    bool ready = false;
    bool failed = false;
};

TranslateGizmoPass::~TranslateGizmoPass() {
    release();
}

void TranslateGizmoPass::release() {
    if (m_impl == nullptr) {
        return;
    }
    releaseCom(m_impl->depthState);
    releaseCom(m_impl->rasterizer);
    releaseCom(m_impl->frameCb);
    releaseCom(m_impl->vb);
    releaseCom(m_impl->layout);
    releaseCom(m_impl->ps);
    releaseCom(m_impl->vs);
    delete m_impl;
    m_impl = nullptr;
}

bool TranslateGizmoPass::ensure(ID3D11Device* device) {
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
        kGizmoShaderSource,
        std::strlen(kGizmoShaderSource),
        "TranslateGizmo",
        nullptr,
        nullptr,
        "vs_main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile gizmo VS";
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
        kGizmoShaderSource,
        std::strlen(kGizmoShaderSource),
        "TranslateGizmo",
        nullptr,
        nullptr,
        "ps_main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile gizmo PS";
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
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };
    hr = device->CreateInputLayout(
        layoutDesc, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_impl->layout);
    releaseCom(vsBlob);
    if (FAILED(hr)) {
        return fail();
    }

    const std::vector<GizmoVertex> vertices = buildGizmoMesh();
    m_impl->vertexCount = static_cast<UINT>(vertices.size());
    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GizmoVertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = vertices.data();
    hr = device->CreateBuffer(&vbDesc, &vbData, &m_impl->vb);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(GizmoFrameCb);
    hr = device->CreateBuffer(&cbDesc, nullptr, &m_impl->frameCb);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&raster, &m_impl->rasterizer);
    if (FAILED(hr)) {
        return fail();
    }

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = device->CreateDepthStencilState(&depth, &m_impl->depthState);
    if (FAILED(hr)) {
        return fail();
    }

    m_impl->ready = true;
    return true;
}

void TranslateGizmoPass::draw(
    ID3D11DeviceContext* context,
    const Mat4& viewProj,
    const Vec3& origin) const {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr) {
        return;
    }

    const Mat4 translated = viewProj * translationMatrix(origin);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* frame = static_cast<GizmoFrameCb*>(mapped.pData);
    std::memcpy(frame->viewProj, translated.m, sizeof(frame->viewProj));
    context->Unmap(m_impl->frameCb, 0);

    const UINT stride = sizeof(GizmoVertex);
    const UINT offset = 0;
    context->IASetInputLayout(m_impl->layout);
    context->IASetVertexBuffers(0, 1, &m_impl->vb, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_impl->vs, nullptr, 0);
    context->PSSetShader(m_impl->ps, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);
    context->Draw(m_impl->vertexCount, 0);
}

} // namespace sat
