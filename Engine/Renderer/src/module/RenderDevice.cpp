#include "RenderState.hpp"

#include <GLFW/glfw3.h>

#include <dxgi1_2.h>
#include <dxgi1_5.h>

#include <iostream>

namespace sat {

bool RenderSession::createBackBuffer() {
    ID3D11Texture2D* buffer = nullptr;
    const HRESULT getBuffer = m_state->swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(getBuffer)) {
        std::cerr << "Failed to get swap chain back buffer\n";
        return false;
    }

    const HRESULT createView =
        m_state->device->CreateRenderTargetView(buffer, nullptr, &m_state->backBuffer);
    buffer->Release();
    if (FAILED(createView)) {
        std::cerr << "Failed to create back buffer view\n";
        return false;
    }
    return true;
}

bool RenderSession::createDevice(void* hwnd, unsigned flags) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_state->window, &width, &height);
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
        &m_state->device,
        &createdLevel,
        &m_state->context);
    (void)createdLevel;
    if (FAILED(created)) {
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(m_state->device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
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

    m_state->allowTearing = false;
    IDXGIFactory5* factory5 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
        BOOL tearingSupported = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &tearingSupported,
                sizeof(tearingSupported))) &&
            tearingSupported) {
            desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            m_state->allowTearing = true;
        }
        factory5->Release();
    }

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
    fullscreen.Windowed = TRUE;

    IDXGISwapChain1* flipChain = nullptr;
    HRESULT swapChainResult = factory->CreateSwapChainForHwnd(
        m_state->device,
        static_cast<HWND>(hwnd),
        &desc,
        &fullscreen,
        nullptr,
        &flipChain);
    if (SUCCEEDED(swapChainResult)) {
        m_state->swapChain = flipChain;
    } else if (swapChainResult == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
        m_state->allowTearing = false;
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
        swapChainResult = factory->CreateSwapChain(m_state->device, &legacy, &m_state->swapChain);
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
    if (SUCCEEDED(m_state->swapChain->GetDesc(&createdDesc)) && createdDesc.BufferDesc.Width > 0 &&
        createdDesc.BufferDesc.Height > 0) {
        m_state->swapWidth = static_cast<int>(createdDesc.BufferDesc.Width);
        m_state->swapHeight = static_cast<int>(createdDesc.BufferDesc.Height);
    } else {
        m_state->swapWidth = width;
        m_state->swapHeight = height;
    }
    return true;
}

void RenderSession::bindWindowTarget() {
    if (!valid()) {
        return;
    }
    m_state->context->OMSetRenderTargets(1, &m_state->backBuffer, nullptr);
    const float clearColor[4] = {0.10f, 0.10f, 0.11f, 1.0f};
    m_state->context->ClearRenderTargetView(m_state->backBuffer, clearColor);
}

bool RenderSession::renderWindow(const GameViewCamera& camera, const SceneDrawList& drawList) {
    if (!valid()) {
        return false;
    }
    // Reuse the scene render path and copy its output to the swap chain without UI.
    resizeGameView(m_state->swapWidth, m_state->swapHeight);
    if (m_state->game.color == nullptr) {
        return false;
    }
    renderGameView(camera, drawList);
    m_state->context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(m_state->swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        std::cerr << "Failed to get window target for game presentation\n";
        return false;
    }
    m_state->context->CopyResource(buffer, m_state->game.color);
    buffer->Release();
    return true;
}

void RenderSession::present() {
    if (m_state->swapChain == nullptr) {
        return;
    }
    // Sync interval 0: presentation is not capped by the monitor refresh rate.
    const UINT flags = m_state->allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    m_state->swapChain->Present(0, flags);
}

std::uintptr_t RenderSession::createTexture(const unsigned char* rgba, int width, int height) {
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
    const HRESULT created = m_state->device->CreateTexture2D(&desc, &data, &texture);
    if (FAILED(created)) {
        std::cerr << "Failed to create texture\n";
        return 0;
    }

    ID3D11ShaderResourceView* view = nullptr;
    const HRESULT viewCreated = m_state->device->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
    if (FAILED(viewCreated)) {
        std::cerr << "Failed to create texture view\n";
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(view);
}

void RenderSession::destroyTexture(std::uintptr_t texture) {
    if (texture == 0) {
        return;
    }
    ID3D11ShaderResourceView* view =
        reinterpret_cast<ID3D11ShaderResourceView*>(texture);
    view->Release();
}

} // namespace sat
