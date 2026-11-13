#pragma once

#include "SceneDraw.hpp"

#include <memory>

struct GLFWwindow;

namespace sat {

class Scene;
struct Entity;
class Renderer;
struct GameViewCamera;

class ViewportPanel {
public:
    ViewportPanel(Scene& scene, Renderer& renderer, GLFWwindow& window);
    ~ViewportPanel();

    ViewportPanel(const ViewportPanel&) = delete;
    ViewportPanel& operator=(const ViewportPanel&) = delete;

    // selectedIndex refers to the authored scene, including when picking a live entity.
    // When focusGameView is true, the Game window is focused this frame.
    void onImGuiRender(int& selectedIndex, const Scene& gameScene, bool sceneReadOnly, bool focusGameView);

    void resetInteraction();
    [[nodiscard]] bool showColliders() const;
    [[nodiscard]] bool showingLiveScene() const;
    [[nodiscard]] TransformGizmoInstance gizmo(const Entity& entity) const;
    [[nodiscard]] bool selectionActivated() const;
    // Camera look, pan, gizmo drag, or pointer motion over the scene image this frame.
    [[nodiscard]] bool sceneViewInteracting() const;

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
