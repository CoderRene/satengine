#include "RenderState.hpp"

#include "ShaderLayout.hpp"

#include <iostream>
#include <vector>

namespace sat {

namespace {

void appendLine(
    std::vector<DebugLineVertex>& vertices,
    float x0,
    float y0,
    float z0,
    float x1,
    float y1,
    float z1,
    float r,
    float g,
    float b,
    float a = 1.0f) {
    vertices.push_back({x0, y0, z0, r, g, b, a});
    vertices.push_back({x1, y1, z1, r, g, b, a});
}

std::vector<DebugLineVertex> buildDebugLineVertices() {
    std::vector<DebugLineVertex> vertices;
    constexpr int kHalfExtent = 10;
    constexpr float kGrid = 0.45f;
    constexpr float kAxisGrid = 0.7f;

    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float t = static_cast<float>(i);
        const bool major = i == 0;
        const float shade = major ? kAxisGrid : kGrid;
        appendLine(vertices, -static_cast<float>(kHalfExtent), 0.0f, t,
                   static_cast<float>(kHalfExtent), 0.0f, t, shade, shade, shade);
        appendLine(vertices, t, 0.0f, -static_cast<float>(kHalfExtent), t, 0.0f,
                   static_cast<float>(kHalfExtent), shade, shade, shade);
    }

    return vertices;
}

} // namespace

void RenderSession::releaseDebugLines() {
    releaseCom(m_state->lineDepthState);
    releaseCom(m_state->lineRasterizer);
    releaseCom(m_state->lineCb);
    releaseCom(m_state->lineVb);
    m_state->lineVertexCount = 0;
    m_state->debugLinesReady = false;
}

bool RenderSession::ensureDebugLines() {
    if (m_state->debugLinesReady) {
        return true;
    }
    if (m_state->debugLinesFailed || m_state->device == nullptr) {
        return false;
    }

    const std::vector<DebugLineVertex> vertices = buildDebugLineVertices();
    m_state->lineVertexCount = static_cast<UINT>(vertices.size());

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(DebugLineVertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = vertices.data();
    HRESULT hr = m_state->device->CreateBuffer(&vbDesc, &vbData, &m_state->lineVb);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line vertex buffer\n";
        releaseDebugLines();
        m_state->debugLinesFailed = true;
        return false;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(GridFrameCb);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_state->device->CreateBuffer(&cbDesc, nullptr, &m_state->lineCb);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line constant buffer\n";
        releaseDebugLines();
        m_state->debugLinesFailed = true;
        return false;
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    hr = m_state->device->CreateRasterizerState(&raster, &m_state->lineRasterizer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line rasterizer\n";
        releaseDebugLines();
        m_state->debugLinesFailed = true;
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    hr = m_state->device->CreateDepthStencilState(&depth, &m_state->lineDepthState);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line depth state\n";
        releaseDebugLines();
        m_state->debugLinesFailed = true;
        return false;
    }

    m_state->debugLinesReady = true;
    return true;
}

} // namespace sat
