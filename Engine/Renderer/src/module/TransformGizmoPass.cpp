#include "TransformGizmoPass.hpp"

#include "ShaderLayout.hpp"

#include "TransformGizmo.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>

#include <array>
#include <cmath>
#include <numbers>
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
    float handle = 0;
};

static_assert(sizeof(GizmoVertex) == 8 * sizeof(float));



static_assert(sizeof(GizmoFrameCb) % 16 == 0);



struct GizmoRange {
    UINT first = 0;
    UINT count = 0;
};
struct GizmoGeometry {
    std::vector<GizmoVertex> vertices;
    std::array<GizmoRange, 3> ranges;
};
void appendTri(std::vector<GizmoVertex>& vertices, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& color, int handle) {
    for (const auto& point : {a, b, c}) {
        vertices.push_back({point.x, point.y, point.z, color.x, color.y, color.z, 1, static_cast<float>(handle)});
    }
}
void appendCube(std::vector<GizmoVertex>& vertices, const Vec3& center, float extent, const Vec3& color, int handle) {
    Vec3 corners[8];
    for (int corner = 0; corner < 8; ++corner) {
        corners[corner] = center + Vec3{corner & 1 ? extent : -extent, corner & 2 ? extent : -extent, corner & 4 ? extent : -extent};
    }
    constexpr int kFaces[6][4] = {{0,1,3,2}, {4,6,7,5}, {0,2,6,4}, {1,5,7,3}, {0,4,5,1}, {2,3,7,6}};
    for (const auto& face : kFaces) {
        appendTri(vertices, corners[face[0]], corners[face[1]], corners[face[2]], color, handle);
        appendTri(vertices, corners[face[0]], corners[face[2]], corners[face[3]], color, handle);
    }
}
void appendAxis(std::vector<GizmoVertex>& vertices, int handle, const Vec3& color, bool scaleHandle) {
    const Vec3 axis = transformGizmoAxis(Mat4{}, handle);
    const Vec3 u = transformGizmoAxis(Mat4{}, (handle + 1) % 3);
    const Vec3 v = transformGizmoAxis(Mat4{}, (handle + 2) % 3);
    constexpr float kConeLength = 0.22f;
    constexpr float kConeRadius = 0.07f;
    constexpr float kShaftRadius = 0.025f;
    const Vec3 base = axis * (scaleHandle ? kTranslateGizmoAxisLength : kTranslateGizmoAxisLength - kConeLength);
    const Vec3 tip = axis * kTranslateGizmoAxisLength;
    for (int segment = 0; segment < 8; ++segment) {
        const float a = segment * (2 * std::numbers::pi_v<float> / 8);
        const float b = (segment + 1) * (2 * std::numbers::pi_v<float> / 8);
        const Vec3 radialA = u * std::cos(a) + v * std::sin(a);
        const Vec3 radialB = u * std::cos(b) + v * std::sin(b);
        const Vec3 first = radialA * kShaftRadius, second = radialB * kShaftRadius;
        appendTri(vertices, first, base + first, base + second, color, handle);
        appendTri(vertices, first, base + second, second, color, handle);
        if (!scaleHandle) {
            appendTri(vertices, tip, base + radialA * kConeRadius, base + radialB * kConeRadius, color, handle);
            appendTri(vertices, base, base + radialB * kConeRadius, base + radialA * kConeRadius, color, handle);
        }
    }
    if (scaleHandle) { appendCube(vertices, tip, kGizmoScaleTipHalfExtent, color, handle); }
}
void appendRing(std::vector<GizmoVertex>& vertices, int handle, const Vec3& color) {
    const Vec3 axis = transformGizmoAxis(Mat4{}, handle);
    const auto point = [&](int segment, int side) {
        const float tubeAngle = side * (2 * std::numbers::pi_v<float> / 6);
        const Vec3 center = transformGizmoRingVertex(handle, segment);
        const Vec3 radial = center * (1.0f / kTranslateGizmoAxisLength);
        return center + (radial * std::cos(tubeAngle) + axis * std::sin(tubeAngle)) * kGizmoTubeRadius;
    };
    for (int segment = 0; segment < kGizmoRingSegments; ++segment) {
        for (int side = 0; side < 6; ++side) {
            const auto a = point(segment, side), b = point(segment + 1, side);
            const auto c = point(segment + 1, side + 1), d = point(segment, side + 1);
            appendTri(vertices, a, b, c, color, handle);
            appendTri(vertices, a, c, d, color, handle);
        }
    }
}
GizmoGeometry buildGizmoMesh() {
    GizmoGeometry geometry;
    geometry.vertices.reserve(8192);
    const Vec3 colors[3] = {{0.95f,0.25f,0.25f}, {0.25f,0.90f,0.30f}, {0.30f,0.45f,0.95f}};
    for (int tool = 0; tool < 3; ++tool) {
        auto& range = geometry.ranges[tool];
        range.first = static_cast<UINT>(geometry.vertices.size());
        for (int axis = 0; axis < 3; ++axis) {
            if (tool == static_cast<int>(TransformTool::Rotate)) { appendRing(geometry.vertices, axis, colors[axis]); }
            else { appendAxis(geometry.vertices, axis, colors[axis], tool == static_cast<int>(TransformTool::Scale)); }
        }
        if (tool == static_cast<int>(TransformTool::Scale)) { appendCube(geometry.vertices, {}, kGizmoCenterHalfExtent, {0.9f,0.9f,0.9f}, 3); }
        range.count = static_cast<UINT>(geometry.vertices.size()) - range.first;
    }
    return geometry;
}

} // namespace

struct TransformGizmoPass::Impl {
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* frameCb = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    std::array<GizmoRange, 3> ranges;
    bool ready = false;
    bool failed = false;
};

TransformGizmoPass::~TransformGizmoPass() {
    release();
}

void TransformGizmoPass::release() {
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

bool TransformGizmoPass::ensure(ID3D11Device* device) {
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


    const GizmoGeometry geometry = buildGizmoMesh();
    const auto& vertices = geometry.vertices;
    m_impl->ranges = geometry.ranges;
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

void TransformGizmoPass::draw(
    ID3D11DeviceContext* context, ShaderProgram program,
    const Mat4& viewProj,
    const TransformGizmoInstance& gizmo,
    float worldScale) const {
    if (m_impl == nullptr || !m_impl->ready || context == nullptr ||
        !std::isfinite(worldScale) || worldScale <= 0.0f) {
        return;
    }

    const int mode = static_cast<int>(gizmo.tool);
    if (mode < 0 || mode >= 3) { return; }
    Mat4 translated = viewProj * translationMatrix(gizmo.origin) * gizmo.basis;
    // Post-multiply uniform scale without rebuilding geometry or moving origin.
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 4; ++row) { translated.m[column * 4 + row] *= worldScale; }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_impl->frameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* frame = static_cast<GizmoFrameCb*>(mapped.pData);
    std::memcpy(frame->viewProj, translated.m, sizeof(frame->viewProj));
    frame->selection[0] = static_cast<float>(gizmo.highlightedHandle);
    frame->selection[1] = gizmo.dragging ? 1.0f : 0.0f;
    frame->selection[2] = frame->selection[3] = 0;
    context->Unmap(m_impl->frameCb, 0);

    const UINT stride = sizeof(GizmoVertex);
    const UINT offset = 0;
    context->IASetInputLayout(program.layout);
    context->IASetVertexBuffers(0, 1, &m_impl->vb, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(program.vertex, nullptr, 0);
    context->PSSetShader(program.pixel, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &m_impl->frameCb);
    context->RSSetState(m_impl->rasterizer);
    context->OMSetDepthStencilState(m_impl->depthState, 0);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->Draw(m_impl->ranges[mode].count, m_impl->ranges[mode].first);
}

} // namespace sat
