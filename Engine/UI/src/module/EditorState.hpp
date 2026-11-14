#pragma once

#include "EditorSession.hpp"

#include "module/AssetImportPanel.hpp"
#include "module/PlaySession.hpp"

#include "ContentPanel.hpp"
#include "EnginePaths.hpp"
#include "GameUi.hpp"
#include "GameBuilder.hpp"
#include "HierarchyPanel.hpp"
#include "InspectorPanel.hpp"
#include "LuaLogPanel.hpp"
#include "Project.hpp"
#include "RecentProjects.hpp"
#include "Renderer.hpp"
#include "SceneDraw.hpp"
#include "SceneRenderData.hpp"
#include "ViewportPanel.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace sat {

struct EditorSession::State {
    State(Scene& scene, Project& project, const Input& input);

    GLFWwindow* window = nullptr;
    Renderer* renderer = nullptr;
    bool glfwReady = false;
    bool dx11Ready = false;
    Project& project;
    Scene& scene;
    SceneRenderData gameRenderData;
    std::vector<MeshInstance> sceneMeshes;
    std::vector<LightInstance> sceneLights;
    std::vector<CameraFrustumInstance> sceneFrustums;
    std::vector<DebugLine> colliderLines;
    RecentProjects recent{std::filesystem::path("recent_projects.txt")};
    const Input& input;
    std::unique_ptr<GameUi> gameUi;
    // Declared before the panels so their destructors still see the play log.
    PlaySession play;
    std::unique_ptr<HierarchyPanel> hierarchy;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<ContentPanel> content;
    std::unique_ptr<AssetImportPanel> assetImports;
    bool closeApproved = false;
    bool openClosePrompt = false;
    bool discardSceneOnClose = false;
    std::unique_ptr<ViewportPanel> viewport;
    std::unique_ptr<LuaLogPanel> luaLog;

    std::string sceneError;
    std::uint64_t savedSceneRevision = 0;
    bool hasSavedScene = false;

    GameBuilder builder{std::filesystem::path(SAT_ENGINE_SOURCE_DIR)};
    bool showBuildStatus = false;
    bool showPhysicsSettings = false;
    bool showAudioSettings = false;
    bool showLightingSettings = false;
    bool openNewProjectModal = false;
    std::filesystem::path newProjectParent;
    char newProjectName[128] = {};

    // Edit mode keeps the previous scene and game targets while these match.
    std::uint64_t sceneViewKey = 0;
    std::uint64_t gameViewKey = 0;
    std::uint64_t sceneContentRevision = 0;
    std::uint64_t gameContentRevision = 0;
    bool sceneViewValid = false;
    bool gameViewValid = false;
    std::chrono::steady_clock::time_point frameStart{};
    std::chrono::steady_clock::time_point editActiveUntil{};
};

} // namespace sat
