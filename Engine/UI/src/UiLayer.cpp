#include "UiLayer.hpp"

#include "module/AssetImportPanel.hpp"
#include "module/PhysicsInspector.hpp"

#include "ContentPanel.hpp"
#include "EnginePaths.hpp"
#include "FileDialog.hpp"
#include "GameBuilder.hpp"
#include "HierarchyPanel.hpp"
#include "InspectorPanel.hpp"
#include "Math.hpp"
#include "LuaRuntime.hpp"
#include "Project.hpp"
#include "PhysicsSystem.hpp"
#include "RecentProjects.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneDraw.hpp"
#include "ColliderLines.hpp"
#include "SceneRenderData.hpp"
#include "ViewportPanel.hpp"
#include "Window.hpp"
#include "ScriptLog.hpp"
#include "LuaLogPanel.hpp"
#include "EditorLuaDefs.hpp"
#include "LuaToolingSync.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx11.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sat {

struct UiLayer::Impl {
    Impl(Scene& scene, Project& project, const Input& input)
    : project(project), scene(scene), physics(scene), scripts(scene, &input, &scriptLog, &physics),
      savedSceneRevision(scene.editRevision()) {
        if (project.isOpen()) { recordLoadedScene(); }
    }
    GLFWwindow* window = nullptr;
    Renderer* renderer = nullptr;
    bool glfwReady = false;
    bool dx11Ready = false;
    Project& project;
    Scene& scene;
    SceneRenderData gameRenderData;
    std::vector<MeshInstance> sceneMeshes;
    std::vector<CameraFrustumInstance> sceneFrustums;
    std::vector<DebugLine> colliderLines;
    RecentProjects recent{std::filesystem::path("recent_projects.txt")};
    std::unique_ptr<HierarchyPanel> hierarchy;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<ContentPanel> content;
    std::unique_ptr<AssetImportPanel> assetImports;
    bool closeApproved = false;
    bool openClosePrompt = false;
    bool discardSceneOnClose = false;
    std::unique_ptr<ViewportPanel> viewport;
    std::unique_ptr<LuaLogPanel> luaLog;

    ScriptLog scriptLog;
    PhysicsSystem physics;
    LuaRuntime scripts;
    Scene editScene;
    bool playMode = false;
    bool focusGameView = false;
    bool focusLuaLog = false;
    std::string sceneError;

    std::uint64_t savedSceneRevision;
    bool hasSavedScene = false;

    [[nodiscard]] bool hasUnsavedChanges() const {
        const Scene& authoredScene = playMode ? editScene : scene;
        return authoredScene.editRevision() != savedSceneRevision ||
            (project.isOpen() && !hasSavedScene);
    }

    void recordLoadedScene() {
        savedSceneRevision = scene.editRevision();
        std::error_code error;
        // Legacy projects without Scene.sat have not saved their default scene.
        hasSavedScene = std::filesystem::is_regular_file(project.root() / "Scene.sat", error);
    }

    bool saveScene() {
        if (!project.isOpen()) { return true; }
        const Scene& authoredScene = playMode ? editScene : scene;
        if (!project.saveScene(authoredScene, sceneError)) { return false; }
        savedSceneRevision = authoredScene.editRevision();
        hasSavedScene = true;
        return true;
    }

    void stopPlay() {
        if (!playMode) {
            return;
        }
        scripts.stop();
        if (!scripts.error().empty()) {
            scriptLog.error(scripts.error());
            focusLuaLog = true;
        }
        scene = std::move(editScene);
        playMode = false;
        focusGameView = false;
        if (viewport) {
            viewport->resetInteraction();
        }
        if (hierarchy) {
            hierarchy->setSelectedIndex(-1);
        }
    }

    void startPlay() {
        assetImports->request([this] { startPlayNow(); });
    }
    void startPlayNow() {
        if (playMode || !project.isOpen()) {
            return;
        }
        if (viewport) { viewport->resetInteraction(); }
        if (!saveScene()) {
            return;
        }
        editScene = scene;
        scriptLog.clear();
        playMode = true;
        if (viewport) {
            viewport->resetInteraction();
        }
        if (!scripts.start(project.root())) {
            stopPlay();
        } else {
            focusGameView = true;
        }
    }
    GameBuilder builder{std::filesystem::path(SAT_ENGINE_SOURCE_DIR)};
    bool showBuildStatus = false;
    bool showPhysicsSettings = false;
    bool openNewProjectModal = false;
    std::filesystem::path newProjectParent;
    char newProjectName[128] = {};

    void openProject(const std::filesystem::path& folder) {
        assetImports->request([this, folder] { (void)openProjectNow(folder); });
    }
    bool openProjectNow(const std::filesystem::path& folder) {
        Project opened;
        if (!opened.open(folder)) {
            recent.remove(folder);
            return false;
        }
        if (project.isOpen() && opened.root() == project.root()) {
            return true;
        }
        Scene loaded;
        if (!opened.loadScene(loaded, sceneError)) {
            return false;
        }
        stopPlay();
        if (viewport) { viewport->resetInteraction(); }
        if (!saveScene()) {
            return false;
        }
        assetImports->clear();
        content->setSelectedAsset({});
        project = std::move(opened);
        scene = std::move(loaded);
        recordLoadedScene();
        renderer->setAssetReader(fileAssetReader(project.root()));
        if (hierarchy) { hierarchy->setSelectedIndex(-1); }
        if (viewport) { viewport->resetInteraction(); }
        recent.add(project.root());
        if (!syncLuaToolingFor(project)) {
            std::cerr << "Warning: failed to sync LuaDefs into the project (Cursor linting may not work)\n";
        }
        return true;
    }

    bool syncLuaToolingFor(const Project& opened) const {
        const auto defs = editorLuaDefsDirectory();
        if (defs.empty()) {
            return false;
        }
        return syncProjectLuaTooling(opened.root(), defs);
    }
};

static void applySatTheme() {
    ImGui::StyleColorsDark(); // start from a known base

    ImGuiStyle& style = ImGui::GetStyle();

    // Shape
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    // Spacing
    style.WindowPadding    = ImVec2(8.0f, 8.0f);
    style.FramePadding     = ImVec2(6.0f, 4.0f);
    style.ItemSpacing      = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing    = 16.0f;
    style.ScrollbarSize    = 12.0f;
    style.GrabMinSize      = 10.0f;

    // Borders
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    // Docking
    style.DockingSeparatorSize = 2.0f;

    ImVec4* c = style.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.13f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);

    // Frames / inputs
    c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);

    // Accent (pick one brand color and reuse it)
    const ImVec4 accent              = ImVec4(0.35f, 0.60f, 0.95f, 1.00f);
    const ImVec4 accentHover         = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
    const ImVec4 accentActive        = ImVec4(0.28f, 0.50f, 0.85f, 1.00f);

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;
    c[ImGuiCol_Button]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = accentHover;
    c[ImGuiCol_ButtonActive]         = accentActive;
    c[ImGuiCol_Header]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.35f, 0.48f, 1.00f);
    c[ImGuiCol_HeaderActive]         = accentActive;
    c[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TabHovered]           = accentHover;
    c[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_TabDimmed]          = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]  = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

    // Text / borders
    c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.30f, 0.34f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accentActive;
}

UiLayer::UiLayer(Window& window, Renderer& renderer, Scene& scene, Project& project)
    : m_impl(std::make_unique<Impl>(scene, project, window.input())) {
    m_impl->window = static_cast<GLFWwindow*>(window.nativeHandle());
    m_impl->renderer = &renderer;
    if (m_impl->window == nullptr) {
        std::cerr << "Failed to start UI: window has no native handle\n";
        return;
    }

    renderer.setAssetReader(fileAssetReader(m_impl->project.root()));
    m_impl->hierarchy = std::make_unique<HierarchyPanel>(m_impl->scene);
    m_impl->inspector = std::make_unique<InspectorPanel>(m_impl->scene, m_impl->project);
    m_impl->assetImports = std::make_unique<AssetImportPanel>(m_impl->project, renderer);
    m_impl->content = std::make_unique<ContentPanel>(m_impl->project, renderer);
    m_impl->content->setSelectionHandler([this](std::string path) {
        if (m_impl->assetImports->active() && m_impl->assetImports->path() == path) { return; }
        m_impl->assetImports->request([this, path = std::move(path)] {
            m_impl->viewport->resetInteraction();
            m_impl->hierarchy->setSelectedIndex(-1);
            if (isMeshAssetPath(path) || isTextureAssetPath(path)) {
                m_impl->assetImports->select(path);
                m_impl->content->setSelectedAsset(path);
            } else {
                m_impl->assetImports->clear();
                m_impl->content->setSelectedAsset({});
            }
        });
    });
    m_impl->viewport = std::make_unique<ViewportPanel>(m_impl->scene, renderer);
    m_impl->luaLog = std::make_unique<LuaLogPanel>(m_impl->scriptLog);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    const char* fontPath = SAT_ENGINE_ASSETS_DIR "/Fonts/JetBrainsMono.ttf";
    if (io.Fonts->AddFontFromFileTTF(fontPath, 16.0f) == nullptr) {
        // Fallback when running from the binary dir (font copied by Game POST_BUILD).
        fontPath = "Fonts/JetBrainsMono.ttf";
        if (io.Fonts->AddFontFromFileTTF(fontPath, 16.0f) == nullptr) {
            std::cerr << "Failed to load UI font: " << fontPath << '\n';
        }
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    applySatTheme();

    if (!ImGui_ImplGlfw_InitForOther(m_impl->window, true)) {
        std::cerr << "Failed to start UI: GLFW backend\n";
        return;
    }
    m_impl->glfwReady = true;
    m_impl->dx11Ready = ImGui_ImplDX11_Init(
        static_cast<ID3D11Device*>(renderer.nativeDevice()),
        static_cast<ID3D11DeviceContext*>(renderer.nativeContext()));
    if (!m_impl->dx11Ready) {
        std::cerr << "Failed to start UI: Direct3D 11 backend\n";
    }
}

UiLayer::~UiLayer() {
    if (m_impl->viewport) { m_impl->viewport->resetInteraction(); }
    m_impl->stopPlay();
    if (!m_impl->discardSceneOnClose && !m_impl->saveScene()) {
        std::cerr << m_impl->sceneError << '\n';
    }
    if (m_impl->dx11Ready) {
        ImGui_ImplDX11_Shutdown();
    }
    if (m_impl->glfwReady) {
        ImGui_ImplGlfw_Shutdown();
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }
}

bool UiLayer::valid() const {
    return m_impl->glfwReady && m_impl->dx11Ready &&
        m_impl->renderer != nullptr && m_impl->renderer->valid();
}

void UiLayer::beginFrame() {
    if (!valid()) {
        return;
    }

    m_impl->builder.poll();
    m_impl->renderer->newFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    const bool canSaveScene = m_impl->project.isOpen() && !m_impl->playMode;
    const ImGuiIO& io = ImGui::GetIO();
    if (canSaveScene && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        const bool saved = m_impl->saveScene();
        (void)saved; // Failure details appear in Scene Errors.
    }
    if (m_impl->playMode && !m_impl->scripts.update(ImGui::GetIO().DeltaTime)) {
        m_impl->stopPlay();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                const std::filesystem::path folder =
                    openFolderDialog(m_impl->window, "Select Parent Folder");
                if (!folder.empty()) {
                    m_impl->assetImports->request([this, folder] {
                        m_impl->newProjectParent = folder;
                        m_impl->newProjectName[0] = '\0';
                        m_impl->openNewProjectModal = true;
                    });
                }
            }
            if (ImGui::MenuItem("Open Project...")) {
                const std::filesystem::path folder =
                    openFolderDialog(m_impl->window, "Open Project Folder");
                if (!folder.empty()) {
                    m_impl->openProject(folder);
                }
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSaveScene)) {
                const bool saved = m_impl->saveScene();
                (void)saved; // Failure details appear in Scene Errors.
            }
            if (ImGui::MenuItem("Reload Assets", nullptr, false, m_impl->project.isOpen() && !m_impl->playMode)) {
                m_impl->assetImports->request([this] {
                    m_impl->renderer->setAssetReader(fileAssetReader(m_impl->project.root()));
                    if (m_impl->assetImports->active()) { m_impl->assetImports->select(m_impl->assetImports->path()); }
                });
            }
            if (ImGui::BeginMenu("Recent...")) {
                const auto& recent = m_impl->recent.entries();
                if (recent.empty()) {
                    ImGui::MenuItem("No recent projects", nullptr, false, false);
                } else {
                    for (const std::filesystem::path& path : recent) {
                        const std::string full = path.string();
                        ImGui::PushID(full.c_str());
                        const std::string label = path.filename().string();
                        if (ImGui::MenuItem(label.c_str())) {
                            m_impl->openProject(path);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", full.c_str());
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            ImGui::MenuItem("Physics Settings", nullptr, &m_impl->showPhysicsSettings);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Build")) {
            const bool building = m_impl->builder.status() == GameBuilder::Status::Building;
            const bool canBuild = m_impl->project.isOpen() && !m_impl->playMode && !building;
            if (ImGui::MenuItem("Build Standalone", nullptr, false, canBuild)) {
                if (m_impl->saveScene()) {
                    const auto obfuscatorFolder = openFolderDialog(m_impl->window,
                        "Optional obfuscator folder (generate.py) - Cancel builds without obfuscation");
                    const bool started = m_impl->builder.start(m_impl->project.root(), obfuscatorFolder);
                    (void)started; // Failure details are displayed in Build Status.
                }
                m_impl->showBuildStatus = true;
            }
            if (!m_impl->project.isOpen()) {
                ImGui::TextDisabled("Open a project to build.");
            } else if (m_impl->playMode) {
                ImGui::TextDisabled("Stop Play mode to build.");
            } else if (building) {
                ImGui::TextDisabled("Build in progress...");
            }
            if (ImGui::MenuItem("Build Status", nullptr, m_impl->showBuildStatus)) {
                m_impl->showBuildStatus = !m_impl->showBuildStatus;
            }
            ImGui::EndMenu();
        }

        if (m_impl->hasUnsavedChanges()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "* Unsaved changes");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", m_impl->project.isOpen()
                    ? "Scene edits have not been saved. Use Ctrl+S or File > Save Scene."
                    : "Open or create a project to save your scene.");
            }
        }

        const bool playing = m_impl->playMode;
        ImGui::BeginDisabled(!playing && !m_impl->project.isOpen());
        ImGui::PushStyleColor(ImGuiCol_Button, playing
            ? ImVec4(0.55f, 0.16f, 0.16f, 1.0f)
            : ImVec4(0.12f, 0.40f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playing
            ? ImVec4(0.70f, 0.22f, 0.22f, 1.0f)
            : ImVec4(0.16f, 0.52f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, playing
            ? ImVec4(0.44f, 0.12f, 0.12f, 1.0f)
            : ImVec4(0.09f, 0.32f, 0.18f, 1.0f));
        // Keep the button within the menu bar's height and stable when toggling.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 0.0f));
        const float buttonWidth = ImGui::CalcTextSize("Stop").x + 24.0f;
        // Center on the host window, keeping clear of the menus at narrow widths.
        const float centeredX = (ImGui::GetWindowWidth() - buttonWidth) * 0.5f;
        if (centeredX > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(centeredX);
        }
        if (ImGui::Button(playing ? "Stop##PlayMode" : "Play##PlayMode", ImVec2(buttonWidth, 0.0f))) {
            if (playing) {
                m_impl->stopPlay();
            } else {
                m_impl->startPlay();
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", playing
                ? "Stop the game and restore the editor scene"
                : (m_impl->project.isOpen()
                    ? "Run enabled entity scripts in the Game view"
                    : "Open or create a project to play"));
        }
        ImGui::EndDisabled();
        if (m_impl->playMode) {
            const float fps = ImGui::GetIO().Framerate;
            char fpsText[32] = {};
            std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", fps);
            const float textWidth = ImGui::CalcTextSize(fpsText).x;
            const float rightPad = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - textWidth - rightPad);
            ImGui::TextUnformatted(fpsText);
        }
        ImGui::EndMenuBar();
    }

    if (m_impl->showPhysicsSettings) {
        if (ImGui::Begin("Physics Settings", &m_impl->showPhysicsSettings)) {
            ImGui::BeginDisabled(m_impl->playMode);
            if (drawPhysicsSettings(m_impl->scene.physicsSettings)) { m_impl->scene.markEdited(); }
            ImGui::EndDisabled();
            if (m_impl->playMode) { ImGui::Text("Active local worlds: %zu", m_impl->physics.frameCount()); }
        }
        ImGui::End();
    }
    if (m_impl->showBuildStatus) {
        ImGui::SetNextWindowSize(ImVec2(680.0f, 440.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Build Status", &m_impl->showBuildStatus)) {
            const auto& builder = m_impl->builder;
            if (builder.status() == GameBuilder::Status::Idle) {
                ImGui::TextUnformatted("Choose Build > Build Standalone to export the open project.");
            } else {
                ImGui::TextWrapped("%s", builder.message().c_str());
                char progressText[64] = {};
                std::snprintf(progressText, sizeof(progressText), "%.0f%% (build stages)", builder.progress() * 100.0f);
                ImGui::ProgressBar(builder.progress(), ImVec2(-1.0f, 0.0f), progressText);
                ImGui::Text("Elapsed: %.0f seconds", builder.elapsedSeconds());
                if (builder.status() == GameBuilder::Status::Building) {
                    ImGui::TextDisabled("Stage progress; compilation time varies. Live output appears below.");
                    if (builder.secondsSinceOutput() >= 10.0) {
                        ImGui::TextDisabled("Last output %.0f seconds ago", builder.secondsSinceOutput());
                    }
                }
                if (!builder.outputFolder().empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("Output: %s", builder.outputFolder().string().c_str());
                    ImGui::TextWrapped("Executable: %s", builder.executablePath().string().c_str());
                    ImGui::TextWrapped("Log: %s", (builder.outputFolder() / "build.log").string().c_str());
                }
                ImGui::Separator();
                if (ImGui::BeginChild("BuildOutput", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                                      ImGuiWindowFlags_HorizontalScrollbar)) {
                    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                    if (builder.logOutput().empty()) {
                        ImGui::TextUnformatted("Waiting for build output...");
                    } else {
                        ImGui::TextUnformatted(builder.logOutput().c_str());
                    }
                    if (atBottom) {
                        ImGui::SetScrollHereY(1.0f);
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    if (!m_impl->renderer->assetErrors().empty()) {
        ImGui::Begin("Asset Errors");
        for (const auto& error : m_impl->renderer->assetErrors()) {
            ImGui::TextWrapped("%s", error.c_str());
        }
        ImGui::BeginDisabled(m_impl->playMode);
        if (ImGui::Button("Reload Assets")) {
            m_impl->assetImports->request([this] {
                    m_impl->renderer->setAssetReader(fileAssetReader(m_impl->project.root()));
                    if (m_impl->assetImports->active()) { m_impl->assetImports->select(m_impl->assetImports->path()); }
                });
        }
        ImGui::EndDisabled();
        ImGui::End();
    }
    if (!m_impl->sceneError.empty()) {
        ImGui::Begin("Scene Errors");
        ImGui::TextWrapped("%s", m_impl->sceneError.c_str());
        ImGui::End();
    }

    ImGuiID dockspaceId = ImGui::GetID("SatEngineDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f));

    static bool appliedDefaultLayout = false;
    if (!appliedDefaultLayout) {
        appliedDefaultLayout = true;
        const char* ini = ImGui::GetIO().IniFilename;
        bool hasSavedLayout = ini != nullptr && std::filesystem::exists(ini);
        if (!hasSavedLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockMain = dockspaceId;
            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

            ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
            ImGui::DockBuilderDockWindow("Inspector", dockRight);
            ImGui::DockBuilderDockWindow("Scene", dockMain);
            ImGui::DockBuilderDockWindow("Game", dockMain);
            ImGui::DockBuilderDockWindow("Content", dockBottom);
            ImGui::DockBuilderDockWindow("Lua Log", dockBottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
    }

    ImGui::End();

    ImGui::BeginDisabled(m_impl->playMode);
    if (m_impl->hierarchy) {
        const int previous = m_impl->hierarchy->selectedIndex();
        m_impl->hierarchy->onImGuiRender(m_impl->playMode);
        if (m_impl->hierarchy->selectionActivated()) {
            const int selected = m_impl->hierarchy->selectedIndex();
            const Entity* entity = selected >= 0 ? m_impl->scene.entity(static_cast<std::size_t>(selected)) : nullptr;
            const EntityId id = entity ? entity->id() : kInvalidEntityId;
            m_impl->hierarchy->setSelectedIndex(previous);
            m_impl->assetImports->request([this, id] {
                m_impl->viewport->resetInteraction();
                m_impl->assetImports->clear(); m_impl->content->setSelectedAsset({});
                const auto index = m_impl->scene.entityIndex(id);
                m_impl->hierarchy->setSelectedIndex(index ? static_cast<int>(*index) : -1);
            });
        }
    }
    ImGui::EndDisabled();

    if (m_impl->viewport) {
        int selected = m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
        const bool focusGame = m_impl->focusGameView;
        m_impl->focusGameView = false;
        m_impl->viewport->onImGuiRender(selected, m_impl->playMode, focusGame);
        if (m_impl->hierarchy && m_impl->viewport->selectionActivated()) {
            const Entity* entity = selected >= 0 ? m_impl->scene.entity(static_cast<std::size_t>(selected)) : nullptr;
            const EntityId id = entity ? entity->id() : kInvalidEntityId;
            m_impl->assetImports->request([this, id] {
                m_impl->viewport->resetInteraction();
                m_impl->assetImports->clear(); m_impl->content->setSelectedAsset({});
                const auto index = m_impl->scene.entityIndex(id);
                m_impl->hierarchy->setSelectedIndex(index ? static_cast<int>(*index) : -1);
            });
        }
    }

    ImGui::BeginDisabled(m_impl->playMode);
    if (m_impl->assetImports->active()) {
        m_impl->assetImports->draw(m_impl->playMode);
    } else if (m_impl->inspector) {
        const int selected =
            m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
        m_impl->inspector->onImGuiRender(selected);
    }
    ImGui::EndDisabled();

    if (m_impl->content) {
        m_impl->content->onImGuiRender();
    }

    if (m_impl->luaLog) {
        const bool focusLog = m_impl->focusLuaLog;
        m_impl->focusLuaLog = false;
        m_impl->luaLog->onImGuiRender(focusLog);
    }

    if (m_impl->openNewProjectModal) {
        ImGui::OpenPopup("New Project");
        m_impl->openNewProjectModal = false;
    }

    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Parent folder:");
        ImGui::TextWrapped("%s", m_impl->newProjectParent.string().c_str());
        ImGui::Spacing();
        ImGui::InputText("Project name", m_impl->newProjectName, sizeof(m_impl->newProjectName));

        const bool canCreate = m_impl->newProjectName[0] != '\0';
        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
            m_impl->stopPlay();
            if (m_impl->viewport) { m_impl->viewport->resetInteraction(); }
            Project created;
            if (m_impl->saveScene() && created.create(
                    m_impl->newProjectParent, std::string(m_impl->newProjectName))) {
                Scene loaded;
                if (created.loadScene(loaded, m_impl->sceneError)) {
                    m_impl->assetImports->clear();
                    m_impl->content->setSelectedAsset({});
                    m_impl->project = std::move(created);
                    m_impl->scene = std::move(loaded);
                    m_impl->recordLoadedScene();
                    m_impl->assetImports->request([this] {
                    m_impl->renderer->setAssetReader(fileAssetReader(m_impl->project.root()));
                    if (m_impl->assetImports->active()) { m_impl->assetImports->select(m_impl->assetImports->path()); }
                });
                    if (m_impl->hierarchy) { m_impl->hierarchy->setSelectedIndex(-1); }
                    m_impl->recent.add(m_impl->project.root());
                    if (!m_impl->syncLuaToolingFor(m_impl->project)) {
                        std::cerr << "Warning: failed to sync LuaDefs into the project (Cursor linting may not work)\n";
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

bool UiLayer::requestClose() {
    if (m_impl->closeApproved ||
        (!m_impl->hasUnsavedChanges() && !m_impl->assetImports->dirty())) {
        return true;
    }
    m_impl->assetImports->request([this] {
        if (m_impl->hasUnsavedChanges()) {
            m_impl->openClosePrompt = true;
        } else {
            m_impl->closeApproved = true;
            glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE);
        }
    });
    return false;
}

void UiLayer::endFrame() {
    if (!valid()) {
        return;
    }

    m_impl->assetImports->drawPrompt();

    if (m_impl->openClosePrompt) {
        ImGui::OpenPopup("Unsaved Scene Changes");
        m_impl->openClosePrompt = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save scene changes before exiting the editor?");
        if (!m_impl->sceneError.empty()) {
            ImGui::TextWrapped("%s", m_impl->sceneError.c_str());
        }
        if (!m_impl->project.isOpen()) {
            ImGui::TextUnformatted("Open or create a project to save this scene.");
        }
        ImGui::BeginDisabled(!m_impl->project.isOpen());
        if (ImGui::Button("Save and Exit")) {
            if (m_impl->saveScene()) {
                m_impl->closeApproved = true;
                glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Exit Without Saving")) {
            m_impl->discardSceneOnClose = true;
            m_impl->closeApproved = true;
            glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    GameViewCamera sceneCamera{};
    if (m_impl->viewport) {
        m_impl->viewport->fillSceneCamera(sceneCamera);
    }

    const int gameViewWidth = m_impl->viewport ? m_impl->viewport->gameViewWidth() : 0;
    const int gameViewHeight = m_impl->viewport ? m_impl->viewport->gameViewHeight() : 0;
    const int sceneViewWidth = m_impl->viewport ? m_impl->viewport->sceneViewWidth() : 0;
    const int sceneViewHeight = m_impl->viewport ? m_impl->viewport->sceneViewHeight() : 0;

    m_impl->gameRenderData.update(m_impl->scene, gameViewWidth, gameViewHeight);
    const auto& gameCamera = m_impl->gameRenderData.camera();

    auto& meshInstances = m_impl->sceneMeshes;
    auto& frustumInstances = m_impl->sceneFrustums;
    auto& colliderLines = m_impl->colliderLines;
    meshInstances.clear(); frustumInstances.clear(); colliderLines.clear();
    int selectedMesh = -1;
    const int selectedEntity =
        m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
    const float sceneAspect =
        (sceneViewHeight > 0)
            ? static_cast<float>(sceneViewWidth) / static_cast<float>(sceneViewHeight)
            : 1.0f;
    const auto& entities = m_impl->scene.entities();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (m_impl->viewport && m_impl->viewport->showColliders()) {
            appendColliderLines(entities[i], sceneCamera.renderOrigin, static_cast<int>(i) == selectedEntity, colliderLines);
        }
        if (entities[i].hasMesh) {
            MeshInstance instance{};
            instance.world = matrixFromTransform(entities[i].transform, sceneCamera.renderOrigin);
            instance.meshPath = entities[i].mesh.meshPath;
            instance.texturePath = entities[i].mesh.texturePath;
            switch (entities[i].mesh.primitive) {
            case MeshComponent::Primitive::Cube:
                instance.primitive = MeshPrimitive::Cube;
                break;
            }
            if (static_cast<int>(i) == selectedEntity) {
                selectedMesh = static_cast<int>(meshInstances.size());
            }
            meshInstances.push_back(instance);
        }
        if (entities[i].hasCamera) {
            CameraFrustumInstance frustum{};
            frustum.position = toVec3(entities[i].transform.position - sceneCamera.renderOrigin);
            frustum.rotation = entities[i].transform.rotation;
            frustum.fieldOfView = entities[i].camera.fieldOfView;
            frustum.nearClip = entities[i].camera.nearClip;
            frustum.farClip = entities[i].camera.farClip;
            frustum.aspect = sceneAspect;
            frustum.selected = (static_cast<int>(i) == selectedEntity);
            frustumInstances.push_back(frustum);
        }
    }

    SceneDrawList sceneDraw{};
    sceneDraw.debugLines = colliderLines.data();
    sceneDraw.debugLineCount = static_cast<int>(colliderLines.size());
    sceneDraw.gridOffset = {-static_cast<float>(std::fmod(sceneCamera.renderOrigin.x, 1.0)),
        -static_cast<float>(sceneCamera.renderOrigin.y), -static_cast<float>(std::fmod(sceneCamera.renderOrigin.z, 1.0))};
    sceneDraw.meshes = meshInstances.empty() ? nullptr : meshInstances.data();
    sceneDraw.meshCount = static_cast<int>(meshInstances.size());
    sceneDraw.selectedMesh = selectedMesh;
    sceneDraw.frustums = frustumInstances.empty() ? nullptr : frustumInstances.data();
    sceneDraw.frustumCount = static_cast<int>(frustumInstances.size());
    sceneDraw.drawGrid = true;
    sceneDraw.drawEditorOverlay = true;
    if (selectedEntity >= 0) {
        const Entity* selected = m_impl->scene.entity(static_cast<std::size_t>(selectedEntity));
        if (selected != nullptr) {
            sceneDraw.showTransformGizmo = true;
            sceneDraw.gizmo = m_impl->viewport->gizmo(*selected);
        }
    }

    m_impl->renderer->renderSceneView(sceneCamera, sceneDraw);
    if (m_impl->gameRenderData.hasCamera()) {
        m_impl->renderer->renderGameView(gameCamera, m_impl->gameRenderData.drawList());
    }
    ImGui::Render();
    m_impl->renderer->bindWindowTarget();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_impl->renderer->present();
}

} // namespace sat
