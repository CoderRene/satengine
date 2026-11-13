#include "CameraFrustumPass.hpp"

#include "ShaderLayout.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>

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
    return inverse(viewFromEuler(position, rotationDegrees));
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
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    std::vector<FrustumVertex> vertices;
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

    HRESULT hr = S_OK;


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
    ID3D11DeviceContext* context, ShaderProgram program,
    const Mat4& viewProj,
    const CameraFrustumInstance* frustums,
    int frustumCount, const DebugLine* lines, int lineCount) {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr ||
        (frustumCount <= 0 && lineCount <= 0)) {
        return;
    }

    auto& vertices = m_impl->vertices;
    vertices.clear();
    vertices.reserve(static_cast<std::size_t>(frustumCount) * 24);
    for (int i = 0; i < frustumCount; ++i) {
        appendFrustum(vertices, frustums[i]);
    }
    for (int i = 0; i < lineCount; ++i) {
        const auto& line = lines[i];
        appendLine(vertices, line.a, line.b, line.color.x, line.color.y, line.color.z, 1.0f);
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
    context->IASetInputLayout(program.layout);
    context->IASetVertexBuffers(0, 1, &m_impl->vb, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->VSSetShader(program.vertex, nullptr, 0);
    context->PSSetShader(program.pixel, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);
    context->Draw(vertexCount, 0);
}

} // namespace sat
