#include "RenderState.hpp"

#include "AssetData.hpp"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <dxgi1_2.h>
#include <dxgi1_5.h>

#include <cstdint>
#include <iostream>
#include <utility>

namespace sat {

RenderSession::RenderSession(void* glfwWindow)
    : m_state(std::make_unique<State>()) {
    m_state->window = static_cast<GLFWwindow*>(glfwWindow);
    if (m_state->window == nullptr) {
        std::cerr << "Failed to create renderer: window has no native handle\n";
        return;
    }

    HWND hwnd = glfwGetWin32Window(m_state->window);
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
#if defined(_DEBUG)
        const bool deviceMissing = m_state->device == nullptr;
#endif
        releaseCom(m_state->backBuffer);
        releaseCom(m_state->swapChain);
        releaseCom(m_state->context);
        releaseCom(m_state->device);
#if defined(_DEBUG)
        if (deviceMissing && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
            std::cerr << "Direct3D debug layer unavailable; creating device without it\n";
            flags &= ~static_cast<unsigned>(D3D11_CREATE_DEVICE_DEBUG);
            if (createDevice(hwnd, flags)) {
                return;
            }
            releaseCom(m_state->backBuffer);
            releaseCom(m_state->swapChain);
            releaseCom(m_state->context);
            releaseCom(m_state->device);
        }
#endif
        std::cerr << "Failed to create Direct3D 11 device\n";
    }
}

RenderSession::~RenderSession() {
    if (m_state->context != nullptr) {
        // Drop context refs to RTVs/SRVs/shaders/buffers before Release.
        m_state->context->ClearState();
        m_state->context->Flush();
    }
    releaseOffscreenView(m_state->game);
    releaseOffscreenView(m_state->scene);
    releaseOffscreenView(m_state->thumbnail);
    m_state->thumbnails.clear();
    releaseDebugLines();
    m_state->shaders.release();
    m_state->meshPass.setAssetReader({});
    m_state->meshPass.release();
    m_state->outlinePass.release();
    m_state->frustumPass.release();
    m_state->gizmoPass.release();
    releaseCom(m_state->backBuffer);
    releaseCom(m_state->swapChain);
    releaseCom(m_state->context);
    releaseCom(m_state->device);
}

bool RenderSession::valid() const {
    return m_state->device != nullptr && m_state->swapChain != nullptr && m_state->backBuffer != nullptr;
}

void RenderSession::invalidateMaterial(std::string_view path) {
    m_state->meshPass.invalidateMaterial(path);
    m_state->thumbnailsDirty = true;
    noteContentChanged();
}

void RenderSession::setShaderReader(AssetReader reader) {
    m_state->shaders.setReader(std::move(reader));
}

bool RenderSession::initializeShaders() {
    return m_state->shaders.ensure(m_state->device);
}

void RenderSession::setAssetReader(AssetReader reader) {
    m_state->thumbnailsDirty = true;
    m_state->meshPass.setAssetReader(std::move(reader));
    noteContentChanged();
}

void RenderSession::setProjectShaderSource(AssetReader reader) {
#ifdef SAT_SHADER_HOT_RELOAD
    if (reader) {
        m_state->shaders.setProjectCompiler([this](std::string_view path, std::string_view source, std::string& bytes) {
            return m_state->hotReload.compileProjectPixel(path, source, bytes);
        });
    } else {
        m_state->shaders.setProjectCompiler({});
    }
#endif
    m_state->shaders.setProjectReader(std::move(reader));
    m_state->thumbnailsDirty = true;
    noteContentChanged();
}

bool RenderSession::reimportAsset(std::string_view path, const AssetImportSettings* settings,
    const AssetImportWriter& writer, std::string& error) {
    const bool imported = m_state->meshPass.reimportAsset(m_state->device, path, settings, writer, error);
    if (imported) {
        if (isTextureAssetPath(path)) { m_state->thumbnailsDirty = true; }
        noteContentChanged();
    }
    return imported;
}

std::string RenderSession::assetInfo(std::string_view path) {
    return m_state->meshPass.assetInfo(path);
}

const std::vector<std::string>& RenderSession::assetErrors() const {
    return m_state->meshPass.assetErrors();
}

bool RenderSession::meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum) {
    return m_state->meshPass.meshBounds(path, minimum, maximum);
}

void RenderSession::noteContentChanged() {
    ++m_state->contentRevision;
}

std::uint64_t RenderSession::contentRevision() const {
    return m_state->contentRevision;
}

void RenderSession::newFrame() {
#ifdef SAT_SHADER_HOT_RELOAD
    if (initializeShaders()) { m_state->hotReload.poll(m_state->device, m_state->shaders); }
#endif
    if (m_state->shaderRevisionSeen != m_state->shaders.revision()) {
        m_state->shaderRevisionSeen = m_state->shaders.revision();
        noteContentChanged();
    }
    if (m_state->thumbnailsDirty || m_state->thumbnailShaderRevision != m_state->shaders.revision()) {
        m_state->thumbnails.clear();
        m_state->thumbnailsDirty = false;
        m_state->thumbnailShaderRevision = m_state->shaders.revision();
    }
    ++m_state->thumbnailFrame;
    m_state->thumbnailBudget = 4;
    if (!valid()) {
        return;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_state->window, &width, &height);
    if (m_state->swapChain != nullptr && width >= 1 && height >= 1 &&
        (width != m_state->swapWidth || height != m_state->swapHeight || m_state->backBuffer == nullptr)) {
        ID3D11RenderTargetView* unbound = nullptr;
        m_state->context->OMSetRenderTargets(1, &unbound, nullptr);
        releaseCom(m_state->backBuffer);

        // Flags must match CreateSwapChainForHwnd (ALLOW_TEARING) or ResizeBuffers fails
        // and leaves the back buffer null — UI stops receiving input after maximize/resize.
        const UINT resizeFlags =
            m_state->allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
        const HRESULT resized = m_state->swapChain->ResizeBuffers(
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
            m_state->swapWidth = width;
            m_state->swapHeight = height;
        }
    }
}

void RenderSession::resizeGameView(int width, int height) {
    resizeOffscreenView(m_state->game, width, height, "game");
}

void RenderSession::resizeSceneView(int width, int height) {
    resizeOffscreenView(m_state->scene, width, height, "scene");
}

void RenderSession::renderGameView(const GameViewCamera& camera, const SceneDrawList& drawList) {
    renderOffscreenView(m_state->game, camera, drawList);
}

void RenderSession::renderSceneView(const GameViewCamera& camera, const SceneDrawList& drawList) {
    renderOffscreenView(m_state->scene, camera, drawList);
}

std::uintptr_t RenderSession::gameColorTexture() const {
    return reinterpret_cast<std::uintptr_t>(m_state->game.srv);
}

std::uintptr_t RenderSession::sceneColorTexture() const {
    return reinterpret_cast<std::uintptr_t>(m_state->scene.srv);
}

int RenderSession::gameViewWidth() const {
    return m_state->game.width;
}

int RenderSession::gameViewHeight() const {
    return m_state->game.height;
}

void* RenderSession::nativeDevice() const {
    return m_state->device;
}

void* RenderSession::nativeContext() const {
    return m_state->context;
}

} // namespace sat
