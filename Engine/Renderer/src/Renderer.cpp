#include "Renderer.hpp"

#include "Math.hpp"
#include "module/CameraFrustumPass.hpp"
#include "module/MeshPass.hpp"
#include "module/SelectionOutlinePass.hpp"
#include "module/TranslateGizmoPass.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <utility>

namespace sat {

struct Renderer::OffscreenView {
    ID3D11Texture2D* color = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    int width = 0;
    int height = 0;
};

struct DebugLineVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct Renderer::Impl {
    GLFWwindow* window = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* backBuffer = nullptr;
    int swapWidth = 0;
    int swapHeight = 0;
    OffscreenView game;
    OffscreenView scene;

    ID3D11VertexShader* lineVs = nullptr;
    ID3D11PixelShader* linePs = nullptr;
    ID3D11InputLayout* lineLayout = nullptr;
    ID3D11Buffer* lineVb = nullptr;
    ID3D11Buffer* lineCb = nullptr;
    ID3D11RasterizerState* lineRasterizer = nullptr;
    ID3D11DepthStencilState* lineDepthState = nullptr;
    UINT lineVertexCount = 0;
    bool debugLinesReady = false;
    bool debugLinesFailed = false;
    bool allowTearing = false;

    MeshPass meshPass;
    SelectionOutlinePass outlinePass;
    CameraFrustumPass frustumPass;
    TranslateGizmoPass gizmoPass;
};

namespace {

template <typename T>
void releaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

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

constexpr const char* kLineShaderSource = R"(
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

} // namespace

Renderer::Renderer(void* glfwWindow)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->window = static_cast<GLFWwindow*>(glfwWindow);
    if (m_impl->window == nullptr) {
        std::cerr << "Failed to create renderer: window has no native handle\n";
        return;
    }

    HWND hwnd = glfwGetWin32Window(m_impl->window);
    if (hwnd == nullptr) {
        std::cerr << "Failed to create renderer: no Win32 window\n";
        return;
    }

    glfwPollEvents();

    unsigned flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    if (!createDevice(hwnd, flags)) {
        const bool deviceMissing = m_impl->device == nullptr;
        releaseCom(m_impl->backBuffer);
        releaseCom(m_impl->swapChain);
        releaseCom(m_impl->context);
        releaseCom(m_impl->device);
#if defined(_DEBUG)
        if (deviceMissing && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
            std::cerr << "Direct3D debug layer unavailable; creating device without it\n";
            flags &= ~static_cast<unsigned>(D3D11_CREATE_DEVICE_DEBUG);
            if (createDevice(hwnd, flags)) {
                return;
            }
            releaseCom(m_impl->backBuffer);
            releaseCom(m_impl->swapChain);
            releaseCom(m_impl->context);
            releaseCom(m_impl->device);
        }
#endif
        std::cerr << "Failed to create Direct3D 11 device\n";
    }
}

void Renderer::releaseOffscreenView(OffscreenView& view) {
    releaseCom(view.dsv);
    releaseCom(view.depth);
    releaseCom(view.srv);
    releaseCom(view.rtv);
    releaseCom(view.color);
    view.width = 0;
    view.height = 0;
}

void Renderer::releaseDebugLines() {
    releaseCom(m_impl->lineDepthState);
    releaseCom(m_impl->lineRasterizer);
    releaseCom(m_impl->lineCb);
    releaseCom(m_impl->lineVb);
    releaseCom(m_impl->lineLayout);
    releaseCom(m_impl->linePs);
    releaseCom(m_impl->lineVs);
    m_impl->lineVertexCount = 0;
    m_impl->debugLinesReady = false;
}

bool Renderer::createBackBuffer() {
    ID3D11Texture2D* buffer = nullptr;
    const HRESULT getBuffer = m_impl->swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(getBuffer)) {
        std::cerr << "Failed to get swap chain back buffer\n";
        return false;
    }

    const HRESULT createView =
        m_impl->device->CreateRenderTargetView(buffer, nullptr, &m_impl->backBuffer);
    buffer->Release();
    if (FAILED(createView)) {
        std::cerr << "Failed to create back buffer view\n";
        return false;
    }
    return true;
}

bool Renderer::createDevice(void* hwnd, unsigned flags) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }

    const D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL createdLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT created = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &m_impl->device,
        &createdLevel,
        &m_impl->context);
    (void)createdLevel;
    if (FAILED(created)) {
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(m_impl->device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
        std::cerr << "Failed to query DXGI device\n";
        return false;
    }

    IDXGIAdapter* adapter = nullptr;
    const HRESULT adapterResult = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(adapterResult)) {
        std::cerr << "Failed to query DXGI adapter\n";
        return false;
    }

    IDXGIFactory2* factory = nullptr;
    const HRESULT factoryResult = adapter->GetParent(IID_PPV_ARGS(&factory));
    adapter->Release();
    if (FAILED(factoryResult)) {
        std::cerr << "Failed to query DXGI factory\n";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    m_impl->allowTearing = false;
    IDXGIFactory5* factory5 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
        BOOL tearingSupported = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &tearingSupported,
                sizeof(tearingSupported))) &&
            tearingSupported) {
            desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            m_impl->allowTearing = true;
        }
        factory5->Release();
    }

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
    fullscreen.Windowed = TRUE;

    IDXGISwapChain1* flipChain = nullptr;
    HRESULT swapChainResult = factory->CreateSwapChainForHwnd(
        m_impl->device,
        static_cast<HWND>(hwnd),
        &desc,
        &fullscreen,
        nullptr,
        &flipChain);
    if (SUCCEEDED(swapChainResult)) {
        m_impl->swapChain = flipChain;
    } else if (swapChainResult == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
        m_impl->allowTearing = false;
        if (flipChain != nullptr) {
            flipChain->Release();
        }
        std::cerr << "Flip-discard presentation is unavailable; using bitblt presentation\n";
        DXGI_SWAP_CHAIN_DESC legacy{};
        legacy.BufferCount = 1;
        legacy.BufferDesc.Width = static_cast<UINT>(width);
        legacy.BufferDesc.Height = static_cast<UINT>(height);
        legacy.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        legacy.BufferDesc.RefreshRate.Numerator = 60;
        legacy.BufferDesc.RefreshRate.Denominator = 1;
        legacy.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        legacy.OutputWindow = static_cast<HWND>(hwnd);
        legacy.SampleDesc.Count = 1;
        legacy.Windowed = TRUE;
        legacy.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        swapChainResult = factory->CreateSwapChain(m_impl->device, &legacy, &m_impl->swapChain);
    }

    if (SUCCEEDED(swapChainResult)) {
        factory->MakeWindowAssociation(static_cast<HWND>(hwnd), DXGI_MWA_NO_ALT_ENTER);
    }
    factory->Release();
    if (FAILED(swapChainResult)) {
        std::cerr << "Failed to create swap chain: 0x" << std::hex
                  << static_cast<unsigned long>(swapChainResult) << std::dec << " (" << width
                  << "x" << height << ")\n";
        return false;
    }

    if (!createBackBuffer()) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC createdDesc{};
    if (SUCCEEDED(m_impl->swapChain->GetDesc(&createdDesc)) && createdDesc.BufferDesc.Width > 0 &&
        createdDesc.BufferDesc.Height > 0) {
        m_impl->swapWidth = static_cast<int>(createdDesc.BufferDesc.Width);
        m_impl->swapHeight = static_cast<int>(createdDesc.BufferDesc.Height);
    } else {
        m_impl->swapWidth = width;
        m_impl->swapHeight = height;
    }
    return true;
}

bool Renderer::ensureDebugLines() {
    if (m_impl->debugLinesReady) {
        return true;
    }
    if (m_impl->debugLinesFailed || m_impl->device == nullptr) {
        return false;
    }

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(
        kLineShaderSource,
        std::strlen(kLineShaderSource),
        "DebugLine",
        nullptr,
        nullptr,
        "vs_main",
        "vs_5_0",
        0,
        0,
        &vsBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile debug line VS";
        if (errorBlob != nullptr) {
            std::cerr << ": " << static_cast<const char*>(errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        std::cerr << '\n';
        m_impl->debugLinesFailed = true;
        return false;
    }
    releaseCom(errorBlob);

    hr = D3DCompile(
        kLineShaderSource,
        std::strlen(kLineShaderSource),
        "DebugLine",
        nullptr,
        nullptr,
        "ps_main",
        "ps_5_0",
        0,
        0,
        &psBlob,
        &errorBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile debug line PS";
        if (errorBlob != nullptr) {
            std::cerr << ": " << static_cast<const char*>(errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        std::cerr << '\n';
        releaseCom(vsBlob);
        m_impl->debugLinesFailed = true;
        return false;
    }
    releaseCom(errorBlob);

    hr = m_impl->device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_impl->lineVs);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line VS\n";
        releaseCom(vsBlob);
        releaseCom(psBlob);
        m_impl->debugLinesFailed = true;
        return false;
    }

    hr = m_impl->device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_impl->linePs);
    releaseCom(psBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line PS\n";
        releaseCom(vsBlob);
        releaseDebugLines();
        m_impl->debugLinesFailed = true;
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };
    hr = m_impl->device->CreateInputLayout(
        layout,
        2,
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &m_impl->lineLayout);
    releaseCom(vsBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line input layout\n";
        releaseDebugLines();
        m_impl->debugLinesFailed = true;
        return false;
    }

    const std::vector<DebugLineVertex> vertices = buildDebugLineVertices();
    m_impl->lineVertexCount = static_cast<UINT>(vertices.size());

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(DebugLineVertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = vertices.data();
    hr = m_impl->device->CreateBuffer(&vbDesc, &vbData, &m_impl->lineVb);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line vertex buffer\n";
        releaseDebugLines();
        m_impl->debugLinesFailed = true;
        return false;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(float) * 16;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_impl->device->CreateBuffer(&cbDesc, nullptr, &m_impl->lineCb);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line constant buffer\n";
        releaseDebugLines();
        m_impl->debugLinesFailed = true;
        return false;
    }

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    hr = m_impl->device->CreateRasterizerState(&raster, &m_impl->lineRasterizer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line rasterizer\n";
        releaseDebugLines();
        m_impl->debugLinesFailed = true;
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    hr = m_impl->device->CreateDepthStencilState(&depth, &m_impl->lineDepthState);
    if (FAILED(hr)) {
        std::cerr << "Failed to create debug line depth state\n";
        releaseDebugLines();
        m_impl->debugLinesFailed = true;
        return false;
    }

    m_impl->debugLinesReady = true;
    return true;
}

void Renderer::resizeOffscreenView(
    OffscreenView& view, int width, int height, const char* label) {
    if (!valid() || width < 1 || height < 1) {
        return;
    }
    if (width == view.width && height == view.height && view.srv != nullptr && view.dsv != nullptr) {
        return;
    }

    releaseOffscreenView(view);

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = static_cast<UINT>(width);
    colorDesc.Height = static_cast<UINT>(height);
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_impl->device->CreateTexture2D(&colorDesc, nullptr, &view.color);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " color target\n";
        releaseOffscreenView(view);
        return;
    }

    hr = m_impl->device->CreateRenderTargetView(view.color, nullptr, &view.rtv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " color RTV\n";
        releaseOffscreenView(view);
        return;
    }

    hr = m_impl->device->CreateShaderResourceView(view.color, nullptr, &view.srv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " color SRV\n";
        releaseOffscreenView(view);
        return;
    }

    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_impl->device->CreateTexture2D(&depthDesc, nullptr, &view.depth);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " depth target\n";
        releaseOffscreenView(view);
        return;
    }

    hr = m_impl->device->CreateDepthStencilView(view.depth, nullptr, &view.dsv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " depth view\n";
        releaseOffscreenView(view);
        return;
    }

    view.width = width;
    view.height = height;
}

void Renderer::renderOffscreenView(
    OffscreenView& view, const GameViewCamera& camera, const SceneDrawList& drawList) {
    if (view.rtv == nullptr || view.dsv == nullptr || m_impl->context == nullptr) {
        return;
    }

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(view.width);
    viewport.Height = static_cast<float>(view.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_impl->context->RSSetViewports(1, &viewport);
    m_impl->context->OMSetRenderTargets(1, &view.rtv, view.dsv);
    m_impl->context->ClearRenderTargetView(view.rtv, camera.clearColor);
    m_impl->context->ClearDepthStencilView(view.dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    Mat4 viewMat{};
    Mat4 projMat{};
    std::memcpy(viewMat.m, camera.view, sizeof(viewMat.m));
    std::memcpy(projMat.m, camera.projection, sizeof(projMat.m));
    const Mat4 viewProj = projMat * viewMat;

    if (drawList.drawGrid && ensureDebugLines()) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(
                m_impl->context->Map(m_impl->lineCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, viewProj.m, sizeof(viewProj.m));
            m_impl->context->Unmap(m_impl->lineCb, 0);

            const UINT stride = sizeof(DebugLineVertex);
            const UINT offset = 0;
            m_impl->context->IASetInputLayout(m_impl->lineLayout);
            m_impl->context->IASetVertexBuffers(0, 1, &m_impl->lineVb, &stride, &offset);
            m_impl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            m_impl->context->VSSetShader(m_impl->lineVs, nullptr, 0);
            m_impl->context->VSSetConstantBuffers(0, 1, &m_impl->lineCb);
            m_impl->context->PSSetShader(m_impl->linePs, nullptr, 0);
            m_impl->context->RSSetState(m_impl->lineRasterizer);
            m_impl->context->OMSetDepthStencilState(m_impl->lineDepthState, 0);
            m_impl->context->Draw(m_impl->lineVertexCount, 0);
        }
    }

    if (m_impl->meshPass.ensure(m_impl->device)) {
        m_impl->meshPass.prepareAssets(m_impl->device, drawList);
        m_impl->meshPass.draw(m_impl->context, viewProj, drawList);
    }

    if (drawList.drawEditorOverlay && drawList.selectedMesh >= 0 &&
        drawList.selectedMesh < drawList.meshCount && drawList.meshes != nullptr) {
        const MeshInstance& selected = drawList.meshes[drawList.selectedMesh];
        const GpuMesh* meshGeometry = m_impl->meshPass.geometry(selected);
        if (meshGeometry != nullptr && m_impl->outlinePass.ensure(m_impl->device)) {
            m_impl->outlinePass.draw(m_impl->context, viewProj, selected, *meshGeometry);
        }
    }

    if (drawList.drawEditorOverlay && drawList.frustumCount > 0 && drawList.frustums != nullptr &&
        m_impl->frustumPass.ensure(m_impl->device)) {
        m_impl->frustumPass.draw(
            m_impl->context, viewProj, drawList.frustums, drawList.frustumCount);
    }

    if (drawList.drawEditorOverlay && drawList.showTranslateGizmo &&
        m_impl->gizmoPass.ensure(m_impl->device)) {
        m_impl->gizmoPass.draw(m_impl->context, viewProj, drawList.translateGizmoOrigin);
    }
}

Renderer::~Renderer() {
    if (m_impl->context != nullptr) {
        // Drop context refs to RTVs/SRVs/shaders/buffers before Release.
        m_impl->context->ClearState();
        m_impl->context->Flush();
    }
    releaseOffscreenView(m_impl->game);
    releaseOffscreenView(m_impl->scene);
    releaseDebugLines();
    m_impl->meshPass.setAssetReader({});
    m_impl->meshPass.release();
    m_impl->outlinePass.release();
    m_impl->frustumPass.release();
    m_impl->gizmoPass.release();
    releaseCom(m_impl->backBuffer);
    releaseCom(m_impl->swapChain);
    releaseCom(m_impl->context);
    releaseCom(m_impl->device);
}

bool Renderer::valid() const {
    return m_impl->device != nullptr && m_impl->swapChain != nullptr && m_impl->backBuffer != nullptr;
}

void Renderer::newFrame() {
    if (!valid()) {
        return;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    if (m_impl->swapChain != nullptr && width >= 1 && height >= 1 &&
        (width != m_impl->swapWidth || height != m_impl->swapHeight || m_impl->backBuffer == nullptr)) {
        ID3D11RenderTargetView* unbound = nullptr;
        m_impl->context->OMSetRenderTargets(1, &unbound, nullptr);
        releaseCom(m_impl->backBuffer);

        // Flags must match CreateSwapChainForHwnd (ALLOW_TEARING) or ResizeBuffers fails
        // and leaves the back buffer null — UI stops receiving input after maximize/resize.
        const UINT resizeFlags =
            m_impl->allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
        const HRESULT resized = m_impl->swapChain->ResizeBuffers(
            0,
            static_cast<UINT>(width),
            static_cast<UINT>(height),
            DXGI_FORMAT_UNKNOWN,
            resizeFlags);
        if (FAILED(resized)) {
            std::cerr << "Failed to resize swap chain: 0x" << std::hex
                      << static_cast<unsigned long>(resized) << std::dec << '\n';
            if (!createBackBuffer()) {
                std::cerr << "Failed to restore back buffer after resize failure\n";
            }
        } else if (createBackBuffer()) {
            m_impl->swapWidth = width;
            m_impl->swapHeight = height;
        }
    }
}

void Renderer::resizeGameView(int width, int height) {
    resizeOffscreenView(m_impl->game, width, height, "game");
}

void Renderer::resizeSceneView(int width, int height) {
    resizeOffscreenView(m_impl->scene, width, height, "scene");
}

bool Renderer::reimportAsset(std::string_view path, const AssetImportSettings* settings,
    const AssetImportWriter& writer, std::string& error) {
    return m_impl->meshPass.reimportAsset(m_impl->device, path, settings, writer, error);
}
std::string Renderer::assetInfo(std::string_view path) { return m_impl->meshPass.assetInfo(path); }
void Renderer::setAssetReader(AssetReader reader) {
    m_impl->meshPass.setAssetReader(std::move(reader));
}
const std::vector<std::string>& Renderer::assetErrors() const {
    return m_impl->meshPass.assetErrors();
}
bool Renderer::meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum) {
    return m_impl->meshPass.meshBounds(path, minimum, maximum);
}

void Renderer::renderGameView(const GameViewCamera& camera, const SceneDrawList& drawList) {
    renderOffscreenView(m_impl->game, camera, drawList);
}

void Renderer::renderSceneView(const GameViewCamera& camera, const SceneDrawList& drawList) {
    renderOffscreenView(m_impl->scene, camera, drawList);
}

std::uintptr_t Renderer::gameColorTexture() const {
    return reinterpret_cast<std::uintptr_t>(m_impl->game.srv);
}

std::uintptr_t Renderer::sceneColorTexture() const {
    return reinterpret_cast<std::uintptr_t>(m_impl->scene.srv);
}

int Renderer::gameViewWidth() const {
    return m_impl->game.width;
}

int Renderer::gameViewHeight() const {
    return m_impl->game.height;
}

std::uintptr_t Renderer::createTexture(const unsigned char* rgba, int width, int height) {
    if (!valid() || rgba == nullptr || width < 1 || height < 1) {
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = rgba;
    data.SysMemPitch = static_cast<UINT>(width) * 4;

    ID3D11Texture2D* texture = nullptr;
    const HRESULT created = m_impl->device->CreateTexture2D(&desc, &data, &texture);
    if (FAILED(created)) {
        std::cerr << "Failed to create texture\n";
        return 0;
    }

    ID3D11ShaderResourceView* view = nullptr;
    const HRESULT viewCreated = m_impl->device->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
    if (FAILED(viewCreated)) {
        std::cerr << "Failed to create texture view\n";
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(view);
}

void Renderer::destroyTexture(std::uintptr_t texture) {
    if (texture == 0) {
        return;
    }
    ID3D11ShaderResourceView* view =
        reinterpret_cast<ID3D11ShaderResourceView*>(texture);
    view->Release();
}

void* Renderer::nativeDevice() const {
    return m_impl->device;
}

void* Renderer::nativeContext() const {
    return m_impl->context;
}

void Renderer::bindWindowTarget() {
    if (!valid()) {
        return;
    }
    m_impl->context->OMSetRenderTargets(1, &m_impl->backBuffer, nullptr);
    const float clearColor[4] = {0.10f, 0.10f, 0.11f, 1.0f};
    m_impl->context->ClearRenderTargetView(m_impl->backBuffer, clearColor);
}

bool Renderer::renderWindow(const GameViewCamera& camera, const SceneDrawList& drawList) {
    if (!valid()) {
        return false;
    }
    // Reuse the scene render path and copy its output to the swap chain without UI.
    resizeGameView(m_impl->swapWidth, m_impl->swapHeight);
    if (m_impl->game.color == nullptr) {
        return false;
    }
    renderGameView(camera, drawList);
    m_impl->context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(m_impl->swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        std::cerr << "Failed to get window target for game presentation\n";
        return false;
    }
    m_impl->context->CopyResource(buffer, m_impl->game.color);
    buffer->Release();
    return true;
}

void Renderer::present() {
    if (m_impl->swapChain == nullptr) {
        return;
    }
    // Sync interval 0: presentation is not capped by the monitor refresh rate.
    const UINT flags = m_impl->allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    m_impl->swapChain->Present(0, flags);
}

} // namespace sat
