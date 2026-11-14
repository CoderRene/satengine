#pragma once

#include "Renderer.hpp"

#include "AssetLibrary.hpp"
#include "Math.hpp"
#include "SceneDraw.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sat {

struct OffscreenView;

// Renderer composition root. Window/device thread only.
class RenderSession {
public:
    explicit RenderSession(void* glfwWindow);
    ~RenderSession();

    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;
    RenderSession(RenderSession&&) = delete;
    RenderSession& operator=(RenderSession&&) = delete;

    [[nodiscard]] bool valid() const;

    void invalidateMaterial(std::string_view path);
    void setShaderReader(AssetReader reader);
    [[nodiscard]] bool initializeShaders();
    void setAssetReader(AssetReader reader);
    void setProjectShaderSource(AssetReader reader);
    [[nodiscard]] bool reimportAsset(std::string_view path, const AssetImportSettings* settings,
        const AssetImportWriter& writer, std::string& error);
    [[nodiscard]] std::string assetInfo(std::string_view path);
    [[nodiscard]] const std::vector<std::string>& assetErrors() const;
    [[nodiscard]] bool meshBounds(std::string_view path, Vec3& minimum, Vec3& maximum);
    void newFrame();
    void resizeGameView(int width, int height);
    void resizeSceneView(int width, int height);
    void renderGameView(const GameViewCamera& camera, const SceneDrawList& drawList);
    void renderSceneView(const GameViewCamera& camera, const SceneDrawList& drawList);
    [[nodiscard]] std::uintptr_t gameColorTexture() const;
    [[nodiscard]] std::uintptr_t sceneColorTexture() const;
    [[nodiscard]] int gameViewWidth() const;
    [[nodiscard]] int gameViewHeight() const;
    [[nodiscard]] std::uint64_t contentRevision() const;
    [[nodiscard]] std::uintptr_t materialThumbnail(std::string_view path);
    [[nodiscard]] std::uintptr_t createTexture(const unsigned char* rgba, int width, int height);
    void destroyTexture(std::uintptr_t texture);
    [[nodiscard]] bool renderWindow(const GameViewCamera& camera, const SceneDrawList& drawList);
    [[nodiscard]] void* uiRenderInterface();
    void renderUi(GameUi& ui, UiTarget target);
    void present();
    [[nodiscard]] void* nativeDevice() const;
    [[nodiscard]] void* nativeContext() const;
    void bindWindowTarget();

private:
    struct State;
    std::unique_ptr<State> m_state;

    void releaseOffscreenView(OffscreenView& view);
    void releaseDebugLines();
    bool createBackBuffer();
    bool createDevice(void* hwnd, unsigned flags);
    bool ensureDebugLines();
    void resizeOffscreenView(OffscreenView& view, int width, int height, const char* label);
    void renderOffscreenView(
        OffscreenView& view, const GameViewCamera& camera, const SceneDrawList& drawList);
    void noteContentChanged();
};

} // namespace sat
