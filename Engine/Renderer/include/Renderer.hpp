#pragma once

#include "SceneDraw.hpp"
#include "AssetLibrary.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sat {

struct GameViewCamera {
    DVec3 renderOrigin{}; // Universe position subtracted before float conversion.
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

class GameUi;
enum class UiTarget { Window, GameView };

class Renderer {
public:
    explicit Renderer(void* glfwWindow);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool valid() const;

    // Window/device thread only. Clears CPU/GPU caches and prior diagnostics.
    // Reader captures must outlive the renderer or the next setAssetReader().
    void invalidateMaterial(std::string_view path);
    // Set before the first frame. Captured reader sources must outlive rendering.
    void setShaderReader(AssetReader reader);
    [[nodiscard]] bool initializeShaders();
    void setAssetReader(AssetReader reader);
    void setProjectShaderSource(AssetReader reader);
    // Frame-boundary/device-thread import. Stage CPU/GPU data, invoke optional
    // persistence callback, then replace only this asset. Failure preserves it.
    [[nodiscard]] bool reimportAsset(std::string_view path, const AssetImportSettings* settings,
        const AssetImportWriter& writer, std::string& error);
    [[nodiscard]] std::string assetInfo(std::string_view path);
    [[nodiscard]] const std::vector<std::string>& assetErrors() const;
    [[nodiscard]] bool meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum);
    void newFrame();
    void resizeGameView(int width, int height);
    void resizeSceneView(int width, int height);
    void renderGameView(const GameViewCamera& camera, const SceneDrawList& drawList = {});
    void renderSceneView(const GameViewCamera& camera, const SceneDrawList& drawList = {});
    [[nodiscard]] std::uintptr_t gameColorTexture() const;
    [[nodiscard]] std::uintptr_t sceneColorTexture() const;
    [[nodiscard]] int gameViewWidth() const;
    [[nodiscard]] int gameViewHeight() const;
    // Bumps when GPU-visible shaders or assets change.
    [[nodiscard]] std::uint64_t contentRevision() const;

    // Borrowed SRV for editor draw data. Valid through the current frame.
    [[nodiscard]] std::uintptr_t materialThumbnail(std::string_view path);

    [[nodiscard]] std::uintptr_t createTexture(const unsigned char* rgba, int width, int height);
    void destroyTexture(std::uintptr_t texture);

    // Render a game scene to the window, with no UI backend required.
    [[nodiscard]] bool renderWindow(const GameViewCamera& camera, const SceneDrawList& drawList = {});
    // Borrowed render interface. Destroy GameUi before destroying this renderer.
    [[nodiscard]] void* uiRenderInterface();
    void renderUi(GameUi& ui, UiTarget target);
    void present();

    // Borrowed D3D11 device/context for optional integrations. Valid for this renderer's lifetime.
    [[nodiscard]] void* nativeDevice() const;
    [[nodiscard]] void* nativeContext() const;
    void bindWindowTarget();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
