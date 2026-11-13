#pragma once

#include "RenderSession.hpp"

#include "module/CameraFrustumPass.hpp"
#include "module/MeshPass.hpp"
#include "module/SelectionOutlinePass.hpp"
#include "module/ShaderLibrary.hpp"
#include "module/TransformGizmoPass.hpp"
#ifdef SAT_SHADER_HOT_RELOAD
#include "module/ShaderHotReload.hpp"
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace sat {

template <typename T>
inline void releaseCom(T*& object) {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

struct DebugLineVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct OffscreenView {
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

struct RenderSession::State {
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
    // Bumps when shaders or imported assets change. Edit-mode views skip redraws while it is stable.
    std::uint64_t contentRevision = 0;
    std::uint64_t shaderRevisionSeen = 0;

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

} // namespace sat
