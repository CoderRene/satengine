#pragma once

#include <memory>

namespace sat {

class Scene;
class Renderer;
struct GameViewCamera;

class ViewportPanel {
public:
    ViewportPanel(Scene& scene, Renderer& renderer);
    ~ViewportPanel();

    ViewportPanel(const ViewportPanel&) = delete;
    ViewportPanel& operator=(const ViewportPanel&) = delete;

    // selectedIndex is read for overlays/gizmos and written on pick.
    // When focusGameView is true, the Game window is focused this frame.
    void onImGuiRender(int& selectedIndex, bool playMode, bool focusGameView);

    void resetInteraction();
    [[nodiscard]] bool selectionActivated() const;

    [[nodiscard]] int sceneViewWidth() const;
    [[nodiscard]] int sceneViewHeight() const;
    [[nodiscard]] int gameViewWidth() const;
    [[nodiscard]] int gameViewHeight() const;

    void fillSceneCamera(GameViewCamera& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
