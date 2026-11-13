#include "Renderer.hpp"

#include "module/ShaderLibrary.hpp"
#include "module/ShaderLayout.hpp"
#ifdef SAT_SHADER_HOT_RELOAD
#include "module/ShaderHotReload.hpp"
#endif

#include "Math.hpp"
#include "module/CameraFrustumPass.hpp"
#include "module/MeshPass.hpp"
#include "module/SelectionOutlinePass.hpp"
#include "module/TransformGizmoPass.hpp"

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
#include <dxgi1_2.h>
#include <dxgi1_5.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <utility>
#include <unordered_map>

namespace sat {

struct Renderer::OffscreenView {
    struct BloomLevel {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> downTarget, upTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> downView, upView;
        int width = 0;
        int height = 0;
    };
    std::vector<BloomLevel> bloomLevels;
    bool bloomAllocationFailed = false;
    ID3D11Texture2D* color = nullptr;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> hdrTarget;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hdrView;
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
    OffscreenView thumbnail;
    struct Thumbnail {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        std::uint64_t lastUsed = 0;
    };
    std::unordered_map<std::string, Thumbnail> thumbnails;
    std::uint64_t thumbnailFrame = 0;
    std::uint64_t thumbnailShaderRevision = 0;
    unsigned thumbnailBudget = 0;
    bool thumbnailsDirty = false;

    ID3D11Buffer* lineVb = nullptr;
    ID3D11Buffer* lineCb = nullptr;
    ID3D11RasterizerState* lineRasterizer = nullptr;
    ID3D11DepthStencilState* lineDepthState = nullptr;
    UINT lineVertexCount = 0;
    bool debugLinesReady = false;
    bool debugLinesFailed = false;
    bool allowTearing = false;

    ShaderLibrary shaders;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> postSampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> bloomCb;
#ifdef SAT_SHADER_HOT_RELOAD
    ShaderHotReload hotReload;
#endif
    MeshPass meshPass;
    SelectionOutlinePass outlinePass;
    CameraFrustumPass frustumPass;
    TransformGizmoPass gizmoPass;
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
    view.bloomLevels.clear();
    view.bloomAllocationFailed = false;
    view.hdrTarget.Reset();
    view.hdrView.Reset();
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

    HRESULT hr = S_OK;


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
    cbDesc.ByteWidth = sizeof(GridFrameCb);
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

    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> hdr;
    if (FAILED(m_impl->device->CreateTexture2D(&colorDesc, nullptr, hdr.GetAddressOf())) ||
        FAILED(m_impl->device->CreateRenderTargetView(hdr.Get(), nullptr, view.hdrTarget.GetAddressOf())) ||
        FAILED(m_impl->device->CreateShaderResourceView(hdr.Get(), nullptr, view.hdrView.GetAddressOf()))) {
        std::cerr << "Failed to create " << label << " HDR target\n";
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

    if (!initializeShaders()) { return; }
    if (!m_impl->postSampler) {
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxAnisotropy = 1;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_impl->device->CreateSamplerState(&sampler, m_impl->postSampler.GetAddressOf()))) {
            return;
        }
    }

    auto* context = m_impl->context;
    if (!m_impl->bloomCb) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(BloomCb);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_impl->device->CreateBuffer(&desc, nullptr, m_impl->bloomCb.GetAddressOf()))) {
            std::cerr << "Cannot create bloom constant buffer\n";
            return;
        }
    }
    auto* hdrTarget = view.hdrTarget.Get();
    context->OMSetRenderTargets(1, &hdrTarget, view.dsv);
    // Camera clear colors are authored in display space. Invert the display transform.
    float hdrClear[4]{};
    for (int i = 0; i < 3; ++i) {
        const float srgb = std::clamp(camera.clearColor[i], 0.0f, 1.0f);
        const float linear = srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        hdrClear[i] = linear / std::max(1.0f - linear, 0.001f);
    }
    context->ClearRenderTargetView(hdrTarget, hdrClear);
    if (drawList.meshCount > 0 && m_impl->meshPass.ensure(m_impl->device)) {
        const Mat4 invView = inverse(viewMat);
        m_impl->meshPass.prepareAssets(m_impl->device, drawList);
        m_impl->meshPass.draw(context, m_impl->shaders.program(ShaderId::Mesh), viewProj,
            {invView.m[12], invView.m[13], invView.m[14]}, drawList, true, m_impl->shaders.program(ShaderId::Shadow).pixel, &m_impl->shaders);
    }

    const auto post = m_impl->shaders.program(ShaderId::PostProcess);
    context->OMSetRenderTargets(1, &view.rtv, nullptr);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(post.vertex, nullptr, 0);
    context->PSSetShader(post.pixel, nullptr, 0);
    auto* sampler = m_impl->postSampler.Get();
    context->PSSetSamplers(0, 1, &sampler);
    const auto& bloom = drawList.bloom;
    BloomCb constants{bloom.threshold, bloom.softKnee * bloom.threshold,
        bloom.enabled ? bloom.intensity : 0.0f, bloom.radius, {bloom.tint[0], bloom.tint[1], bloom.tint[2]},
        bloom.highlightClamp, static_cast<unsigned>(std::clamp(bloom.quality, 0, 2) + 1), 0, 0, 0};
    auto* bloomBuffer = m_impl->bloomCb.Get();
    context->PSSetConstantBuffers(0, 1, &bloomBuffer);
    const auto drawPost = [&](ID3D11RenderTargetView* target, ID3D11ShaderResourceView* source,
        ID3D11ShaderResourceView* secondary, int width, int height) {
        const D3D11_VIEWPORT passViewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        context->RSSetViewports(1, &passViewport);
        context->OMSetRenderTargets(1, &target, nullptr);
        ID3D11ShaderResourceView* inputs[]{source, secondary};
        context->PSSetShaderResources(0, 2, inputs);
        context->UpdateSubresource(bloomBuffer, 0, nullptr, &constants, 0, 0);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* unbound[2]{};
        context->PSSetShaderResources(0, 2, unbound);
    };

    ID3D11ShaderResourceView* glow = nullptr;
    if (constants.intensity > 0 && bloom.radius > 0 && bloom.highlightClamp > 0) {
        // Radius selects the deepest scale. Blend in the last level as radius increases.
        const float scale = std::clamp(std::log2(std::max(bloom.radius, 2.0f) / 2.0f), 0.0f, 5.0f);
        const auto wanted = static_cast<std::size_t>(std::ceil(scale)) + 1;
        int width = view.width, height = view.height;
        std::size_t count = 0;
        for (; count < wanted; ++count) {
            width = (width + 1) / 2;
            height = (height + 1) / 2;
            if (count == view.bloomLevels.size()) {
                if (view.bloomAllocationFailed) { break; }
                OffscreenView::BloomLevel level;
                level.width = width;
                level.height = height;
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = static_cast<UINT>(width);
                desc.Height = static_cast<UINT>(height);
                desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
                desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                const auto create = [&](auto& target, auto& resource) {
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                    return SUCCEEDED(m_impl->device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) &&
                        SUCCEEDED(m_impl->device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf())) &&
                        SUCCEEDED(m_impl->device->CreateShaderResourceView(texture.Get(), nullptr, resource.GetAddressOf()));
                };
                if (!create(level.downTarget, level.downView) || !create(level.upTarget, level.upView)) {
                    std::cerr << "Cannot allocate bloom pyramid level\n";
                    view.bloomAllocationFailed = true;
                    break;
                }
                view.bloomLevels.push_back(std::move(level));
            }
            auto& level = view.bloomLevels[count];
            constants.pass = count == 0 ? 1u : 2u;
            drawPost(level.downTarget.Get(), count == 0 ? view.hdrView.Get() :
                view.bloomLevels[count - 1].downView.Get(), nullptr, width, height);
            if (width == 1 && height == 1) { ++count; break; }
        }
        if (count != 0) {
            glow = view.bloomLevels[count - 1].downView.Get();
            constants.pass = 3;
            for (std::size_t i = count - 1; i > 0; --i) {
                auto& level = view.bloomLevels[i - 1];
                constants.coarseWeight = 0.5f * std::clamp(scale - static_cast<float>(i - 1), 0.0f, 1.0f);
                drawPost(level.upTarget.Get(), level.downView.Get(), glow, level.width, level.height);
                glow = level.upView.Get();
            }
        }
    }
    constants.pass = 0;
    if (!glow) { constants.intensity = 0; }
    drawPost(view.rtv, view.hdrView.Get(), glow, view.width, view.height);
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &view.rtv, view.dsv);

    if (drawList.drawGrid && ensureDebugLines()) {
        const auto program = m_impl->shaders.program(ShaderId::Grid);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(
                m_impl->context->Map(m_impl->lineCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const Mat4 gridViewProj = viewProj * translationMatrix(drawList.gridOffset);
            std::memcpy(mapped.pData, gridViewProj.m, sizeof(gridViewProj.m));
            m_impl->context->Unmap(m_impl->lineCb, 0);

            const UINT stride = sizeof(DebugLineVertex);
            const UINT offset = 0;
            m_impl->context->IASetInputLayout(program.layout);
            m_impl->context->IASetVertexBuffers(0, 1, &m_impl->lineVb, &stride, &offset);
            m_impl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            m_impl->context->VSSetShader(program.vertex, nullptr, 0);
            m_impl->context->VSSetConstantBuffers(0, 1, &m_impl->lineCb);
            m_impl->context->PSSetShader(program.pixel, nullptr, 0);
            m_impl->context->RSSetState(m_impl->lineRasterizer);
            m_impl->context->OMSetDepthStencilState(m_impl->lineDepthState, 0);
            m_impl->context->Draw(m_impl->lineVertexCount, 0);
        }
    }

    if (drawList.drawEditorOverlay && drawList.selectedMesh >= 0 &&
        drawList.selectedMesh < drawList.meshCount && drawList.meshes != nullptr) {
        const MeshInstance& selected = drawList.meshes[drawList.selectedMesh];
        const GpuMesh* meshGeometry = m_impl->meshPass.geometry(selected);
        if (meshGeometry != nullptr && m_impl->outlinePass.ensure(m_impl->device)) {
            m_impl->outlinePass.draw(m_impl->context, m_impl->shaders.program(ShaderId::SelectionOutline), viewProj, selected, *meshGeometry);
        }
    }

    if (drawList.drawEditorOverlay && (drawList.frustumCount > 0 || drawList.debugLineCount > 0) &&
        m_impl->frustumPass.ensure(m_impl->device)) {
        m_impl->frustumPass.draw(
            m_impl->context, m_impl->shaders.program(ShaderId::CameraFrustum), viewProj, drawList.frustums, drawList.frustumCount, drawList.debugLines, drawList.debugLineCount);
    }

    if (drawList.drawEditorOverlay && drawList.showTransformGizmo &&
        m_impl->gizmoPass.ensure(m_impl->device)) {
        const float scale = translateGizmoScale(drawList.gizmo.origin, viewMat, projMat,
            static_cast<float>(view.height));
        if (scale > 0.0f) {
            m_impl->gizmoPass.draw(m_impl->context, m_impl->shaders.program(ShaderId::TransformGizmo), viewProj, drawList.gizmo, scale);
        }
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
    releaseOffscreenView(m_impl->thumbnail);
    m_impl->thumbnails.clear();
    releaseDebugLines();
    m_impl->shaders.release();
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
#ifdef SAT_SHADER_HOT_RELOAD
    if (initializeShaders()) { m_impl->hotReload.poll(m_impl->device, m_impl->shaders); }
#endif
    if (m_impl->thumbnailsDirty || m_impl->thumbnailShaderRevision != m_impl->shaders.revision()) {
        m_impl->thumbnails.clear();
        m_impl->thumbnailsDirty = false;
        m_impl->thumbnailShaderRevision = m_impl->shaders.revision();
    }
    ++m_impl->thumbnailFrame;
    m_impl->thumbnailBudget = 4;
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
    const bool imported = m_impl->meshPass.reimportAsset(m_impl->device, path, settings, writer, error);
    if (imported && isTextureAssetPath(path)) { m_impl->thumbnailsDirty = true; }
    return imported;
}
std::string Renderer::assetInfo(std::string_view path) { return m_impl->meshPass.assetInfo(path); }
void Renderer::invalidateMaterial(std::string_view path) {
    m_impl->meshPass.invalidateMaterial(path);
    m_impl->thumbnailsDirty = true;
}

void Renderer::setShaderReader(AssetReader reader) {
    m_impl->shaders.setReader(std::move(reader));
}

bool Renderer::initializeShaders() {
    return m_impl->shaders.ensure(m_impl->device);
}

void Renderer::setProjectShaderSource(AssetReader reader) {
#ifdef SAT_SHADER_HOT_RELOAD
    if (reader) {
        m_impl->shaders.setProjectCompiler([this](std::string_view path, std::string_view source, std::string& bytes) {
            return m_impl->hotReload.compileProjectPixel(path, source, bytes);
        });
    } else {
        m_impl->shaders.setProjectCompiler({});
    }
#endif
    m_impl->shaders.setProjectReader(std::move(reader));
    m_impl->thumbnailsDirty = true;
}

void Renderer::setAssetReader(AssetReader reader) {
    m_impl->thumbnailsDirty = true;
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

std::uintptr_t Renderer::materialThumbnail(std::string_view path) {
    if (m_impl->thumbnailsDirty || !isMaterialAssetPath(path)) { return 0; }
    std::string key(path);
    auto& cache = m_impl->thumbnails;
    if (const auto found = cache.find(key); found != cache.end()) {
        found->second.lastUsed = m_impl->thumbnailFrame;
        return reinterpret_cast<std::uintptr_t>(found->second.view.Get());
    }
    if (m_impl->thumbnailBudget == 0 || !valid() || !initializeShaders()) { return 0; }
    constexpr std::size_t kCacheLimit = 256;
    if (cache.size() >= kCacheLimit) {
        const auto oldest = std::min_element(cache.begin(), cache.end(), [](const auto& a, const auto& b) {
            return a.second.lastUsed < b.second.lastUsed;
        });
        if (oldest->second.lastUsed == m_impl->thumbnailFrame) { return 0; }
        cache.erase(oldest);
    }
    --m_impl->thumbnailBudget;

    constexpr int kSize = 96;
    auto& preview = m_impl->thumbnail;
    resizeOffscreenView(preview, kSize, kSize, "material thumbnail");
    if (!preview.rtv || !m_impl->meshPass.ensure(m_impl->device)) { return 0; }

    GameViewCamera camera;
    camera.clearColor[0] = 0.16f;
    camera.clearColor[1] = 0.17f;
    camera.clearColor[2] = 0.19f;
    const auto view = lookAt({2.2f, 1.7f, -2.5f}, {}, {0, 1, 0});
    const auto projection = perspective(32, 1, 0.1f, 10);
    std::memcpy(camera.view, view.m, sizeof(camera.view));
    std::memcpy(camera.projection, projection.m, sizeof(camera.projection));
    MeshInstance mesh;
    mesh.materialPath = path;
    const LightInstance lights[] = {
        {{}, normalize({0.3f, -0.65f, 1.0f}), {3.5f, 3.2f, 2.8f}},
        {{}, normalize({-1.0f, -0.25f, 0.15f}), {1.1f, 1.4f, 2.1f}},
    };
    SceneDrawList draw;
    draw.meshes = &mesh;
    draw.meshCount = 1;
    draw.lights = lights;
    draw.lightCount = 2;
    draw.directionalShadows = false;
    draw.bloom.enabled = false;
    m_impl->meshPass.prepareAssets(m_impl->device, draw);
    auto& entry = cache.try_emplace(std::move(key)).first->second;
    entry.lastUsed = m_impl->thumbnailFrame;
    if (!m_impl->meshPass.material(path)) { return 0; }
    renderOffscreenView(preview, camera, draw);

    // Snapshot the shared target. ImGui consumes the cached SRV at end of frame.
    D3D11_TEXTURE2D_DESC desc{};
    preview.color->GetDesc(&desc);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
    if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, image.GetAddressOf())) ||
        FAILED(m_impl->device->CreateShaderResourceView(image.Get(), nullptr, entry.view.GetAddressOf()))) {
        return 0;
    }
    m_impl->context->OMSetRenderTargets(0, nullptr, nullptr);
    m_impl->context->CopyResource(image.Get(), preview.color);
    return reinterpret_cast<std::uintptr_t>(entry.view.Get());
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
