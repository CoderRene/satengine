#pragma once

#include "SceneDraw.hpp"

#include <cstdint>
#include <memory>

namespace sat {

struct GameViewCamera {
    float clearColor[4] = {0.12f, 0.16f, 0.24f, 1.0f};
    float view[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    float projection[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
};

class Renderer {
public:
    explicit Renderer(void* glfwWindow);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool valid() const;

    void newFrame();
    void resizeGameView(int width, int height);
    void resizeSceneView(int width, int height);
    void renderGameView(const GameViewCamera& camera, const SceneDrawList& drawList = {});
    void renderSceneView(const GameViewCamera& camera, const SceneDrawList& drawList = {});
    [[nodiscard]] std::uintptr_t gameColorTexture() const;
    [[nodiscard]] std::uintptr_t sceneColorTexture() const;
    [[nodiscard]] int gameViewWidth() const;
    [[nodiscard]] int gameViewHeight() const;

    [[nodiscard]] std::uintptr_t createTexture(const unsigned char* rgba, int width, int height);
    void destroyTexture(std::uintptr_t texture);

    // Render a game scene to the window, with no UI backend required.
    [[nodiscard]] bool renderWindow(const GameViewCamera& camera, const SceneDrawList& drawList = {});
    void present();

    // Borrowed D3D11 device/context for optional integrations. Valid for this renderer's lifetime.
    [[nodiscard]] void* nativeDevice() const;
    [[nodiscard]] void* nativeContext() const;
    void bindWindowTarget();

private:
    struct Impl;
    struct OffscreenView;

    void releaseOffscreenView(OffscreenView& view);
    void releaseDebugLines();
    bool createBackBuffer();
    bool createDevice(void* hwnd, unsigned flags);
    bool ensureDebugLines();
    void resizeOffscreenView(OffscreenView& view, int width, int height, const char* label);
    void renderOffscreenView(
        OffscreenView& view, const GameViewCamera& camera, const SceneDrawList& drawList);

    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
