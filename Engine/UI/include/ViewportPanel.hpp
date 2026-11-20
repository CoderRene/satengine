#pragma once

#include "SceneDraw.hpp"

#include <memory>
#include <span>
#include <unordered_set>

struct GLFWwindow;

namespace sat {

class Scene;
struct Entity;
class Renderer;
class GameUi;
class Input;
struct GameViewCamera;

class ViewportPanel {
public:
    ViewportPanel(Scene& scene, Renderer& renderer, GLFWwindow& window);
    ~ViewportPanel();

    ViewportPanel(const ViewportPanel&) = delete;
    ViewportPanel& operator=(const ViewportPanel&) = delete;

    // selectedIndex refers to the authored scene, including when picking a live entity.
    // When focusGameView is true, the Game window is focused this frame.
    void onImGuiRender(int& selectedIndex, const std::unordered_set<EntityId>& selection, const Scene& gameScene, bool sceneReadOnly, bool focusGameView,
        GameUi* ui = nullptr, const Input* input = nullptr);

    // Append the overlay after the game target is rendered, before ImGui::Render().
    [[nodiscard]] bool gameStatsVisible() const;
    void drawGameStats();
    void resetInteraction();
    [[nodiscard]] bool showColliders() const;
    [[nodiscard]] bool showingLiveScene() const;
    [[nodiscard]] TransformGizmoInstance gizmo(const Entity& entity, const Scene& visibleScene) const;
    [[nodiscard]] bool transformActive() const;
    [[nodiscard]] std::span<const EntityId> transformTargets() const;
    [[nodiscard]] bool selectionToggle() const;
    [[nodiscard]] bool selectionActivated() const;
    [[nodiscard]] bool deleteRequested() const;
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
