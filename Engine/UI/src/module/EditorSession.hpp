#pragma once

#include "Scene.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace sat {

class Input;
class Project;
class Renderer;

// Editor composition root. Window thread only.
// The borrowed scene, project, and input must outlive this object.
class EditorSession {
public:
    EditorSession(Scene& scene, Project& project, const Input& input);
    ~EditorSession();
    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;
    EditorSession(EditorSession&&) = delete;
    EditorSession& operator=(EditorSession&&) = delete;

    void bind(void* nativeWindow, Renderer& renderer);
    [[nodiscard]] bool windowBound() const;
    void initialize();

    [[nodiscard]] bool backendsReady() const;
    [[nodiscard]] bool rendererValid() const;
    void pollSystems();
    void beginRendererFrame();
    void bindWindowTarget();
    void present();
    // Edit mode waits out the rest of the frame. Play mode returns immediately.
    void paceEditor();

    [[nodiscard]] bool canSaveScene() const;
    bool saveScene();
    void tickScripts(float deltaSeconds);
    void drawInterface(bool canSaveScene);
    [[nodiscard]] bool requestClose();
    void drawPrompts();
    void renderViews();
    void prepareClose();

private:
    struct State;
    std::unique_ptr<State> m_state;

    void shutdown();
    void recordLoadedScene();
    [[nodiscard]] bool hasUnsavedChanges() const;
    void stopPlay();
    void discardPlayEdits();
    void afterStoppingPlay(std::function<void()> action);
    void startPlay();
    void startPlayNow();
    void openProject(const std::filesystem::path& folder);
    void openProjectNow(const std::filesystem::path& folder);
    void reloadAssets();
    void selectEntity(EntityId id, bool keepHierarchySelection = false);
    void syncLuaTooling() const;
    [[nodiscard]] bool createProjectFromDialog();
    static bool createProjectThunk(void* context);
    void drawMenuBar(bool canSaveScene);
    void drawPanels();
};

} // namespace sat
