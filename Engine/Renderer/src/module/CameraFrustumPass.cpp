#include "CameraFrustumPass.hpp"

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

struct FrustumVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct FrustumFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
};

constexpr const char* kFrustumShaderSource = R"(
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

void appendLine(
    std::vector<FrustumVertex>& out,
    const Vec3& a,
    const Vec3& b,
    float r,
    float g,
    float bCol,
    float aCol) {
    out.push_back({a.x, a.y, a.z, r, g, bCol, aCol});
    out.push_back({b.x, b.y, b.z, r, g, bCol, aCol});
}

Mat4 cameraWorldMatrix(const Vec3& position, const Vec3& rotationDegrees) {
    const float pitch = rotationDegrees.x * kDegToRad;
    const float yaw = rotationDegrees.y * kDegToRad;
    const float cosPitch = std::cos(pitch);
    const Vec3 forward = normalize(Vec3{
        cosPitch * std::sin(yaw),
        std::sin(pitch),
        cosPitch * std::cos(yaw),
    });
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    const Vec3 right = normalize(cross(worldUp, forward));
    const Vec3 up = cross(forward, right);

    Mat4 out = Mat4::identity();
    out.m[0] = right.x;
    out.m[1] = right.y;
    out.m[2] = right.z;
    out.m[4] = up.x;
    out.m[5] = up.y;
    out.m[6] = up.z;
    out.m[8] = forward.x;
    out.m[9] = forward.y;
    out.m[10] = forward.z;
    out.m[12] = position.x;
    out.m[13] = position.y;
    out.m[14] = position.z;
    return out;
}

void appendFrustum(
    std::vector<FrustumVertex>& out,
    const CameraFrustumInstance& frustum) {
    float nearPlane = frustum.nearClip;
    float farPlane = frustum.farClip;
    if (nearPlane < 0.001f) {
        nearPlane = 0.001f;
    }
    if (farPlane <= nearPlane) {
        farPlane = nearPlane + 1.0f;
    }

    // Cap far draw distance so huge farClip values stay readable in the editor.
    constexpr float kMaxFrustumFar = 25.0f;
    if (farPlane > kMaxFrustumFar) {
        farPlane = kMaxFrustumFar;
    }

    const float aspect = (frustum.aspect > 0.0001f) ? frustum.aspect : 1.0f;
    const float tanHalfFov = std::tan(frustum.fieldOfView * kDegToRad * 0.5f);
    const float nearH = tanHalfFov * nearPlane;
    const float nearW = nearH * aspect;
    const float farH = tanHalfFov * farPlane;
    const float farW = farH * aspect;

    const Vec3 local[8] = {
        {-nearW, -nearH, nearPlane},
        {nearW, -nearH, nearPlane},
        {nearW, nearH, nearPlane},
        {-nearW, nearH, nearPlane},
        {-farW, -farH, farPlane},
        {farW, -farH, farPlane},
        {farW, farH, farPlane},
        {-farW, farH, farPlane},
    };

    const Mat4 world = cameraWorldMatrix(frustum.position, frustum.rotation);
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = transformPoint(world, local[i]);
    }

    const float r = frustum.selected ? 0.45f : 0.30f;
    const float g = frustum.selected ? 0.90f : 0.70f;
    const float b = frustum.selected ? 1.00f : 0.85f;
    const float a = frustum.selected ? 1.0f : 0.85f;

    // Near rect
    appendLine(out, corners[0], corners[1], r, g, b, a);
    appendLine(out, corners[1], corners[2], r, g, b, a);
    appendLine(out, corners[2], corners[3], r, g, b, a);
    appendLine(out, corners[3], corners[0], r, g, b, a);
    // Far rect
    appendLine(out, corners[4], corners[5], r, g, b, a);
    appendLine(out, corners[5], corners[6], r, g, b, a);
    appendLine(out, corners[6], corners[7], r, g, b, a);
    appendLine(out, corners[7], corners[4], r, g, b, a);
    // Sides
    appendLine(out, corners[0], corners[4], r, g, b, a);
    appendLine(out, corners[1], corners[5], r, g, b, a);
    appendLine(out, corners[2], corners[6], r, g, b, a);
    appendLine(out, corners[3], corners[7], r, g, b, a);
}

} // namespace

struct CameraFrustumPass::Impl {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    UINT capacity = 0;
    bool ready = false;
    bool failed = false;
};

CameraFrustumPass::~CameraFrustumPass() {
    release();
}

void CameraFrustumPass::release() {
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

bool CameraFrustumPass::ensure(ID3D11Device* device) {
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
        kFrustumShaderSource,
        std::strlen(kFrustumShaderSource),
        "CameraFrustum",
        nullptr,
        nullptr,
        "vs_main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile camera frustum VS";
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
        kFrustumShaderSource,
        std::strlen(kFrustumShaderSource),
        "CameraFrustum",
        nullptr,
        nullptr,
        "ps_main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile camera frustum PS";
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

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(FrustumFrameCb);
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
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device->CreateDepthStencilState(&depth, &m_impl->depthState);
    if (FAILED(hr)) {
        return fail();
    }

    m_impl->ready = true;
    return true;
}

void CameraFrustumPass::draw(
    ID3D11DeviceContext* context,
    const Mat4& viewProj,
    const CameraFrustumInstance* frustums,
    int frustumCount) {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr || frustums == nullptr ||
        frustumCount <= 0) {
        return;
    }

    std::vector<FrustumVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(frustumCount) * 24);
    for (int i = 0; i < frustumCount; ++i) {
        appendFrustum(vertices, frustums[i]);
    }
    if (vertices.empty()) {
        return;
    }

    const UINT vertexCount = static_cast<UINT>(vertices.size());
    ID3D11Device* device = nullptr;
    context->GetDevice(&device);
    if (device == nullptr) {
        return;
    }

    if (m_impl->vb == nullptr || m_impl->capacity < vertexCount) {
        releaseCom(m_impl->vb);
        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = static_cast<UINT>(sizeof(FrustumVertex) * vertexCount);
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&vbDesc, nullptr, &m_impl->vb))) {
            device->Release();
            return;
        }
        m_impl->capacity = vertexCount;
    }
    device->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_impl->vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, vertices.data(), sizeof(FrustumVertex) * vertexCount);
    context->Unmap(m_impl->vb, 0);

    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* frame = static_cast<FrustumFrameCb*>(mapped.pData);
    std::memcpy(frame->viewProj, viewProj.m, sizeof(frame->viewProj));
    context->Unmap(m_impl->frameCb, 0);

    const UINT stride = sizeof(FrustumVertex);
    const UINT offset = 0;
    context->IASetInputLayout(m_impl->layout);
    context->IASetVertexBuffers(0, 1, &m_impl->vb, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->VSSetShader(m_impl->vs, nullptr, 0);
    context->PSSetShader(m_impl->ps, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);
    context->Draw(vertexCount, 0);
}

} // namespace sat
