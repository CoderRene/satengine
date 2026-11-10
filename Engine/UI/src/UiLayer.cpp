#include "UiLayer.hpp"

#include "ContentPanel.hpp"
#include "EnginePaths.hpp"
#include "FileDialog.hpp"
#include "GameBuilder.hpp"
#include "HierarchyPanel.hpp"
#include "InspectorPanel.hpp"
#include "Math.hpp"
#include "LuaRuntime.hpp"
#include "Project.hpp"
#include "RecentProjects.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneDraw.hpp"
#include "SceneRenderData.hpp"
#include "ViewportPanel.hpp"
#include "Window.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx11.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sat {

struct UiLayer::Impl {
    Impl(Scene& scene, Project& project, const Input& input) : project(project), scene(scene), scripts(scene, &input) {}
    GLFWwindow* window = nullptr;
    Renderer* renderer = nullptr;
    bool glfwReady = false;
    bool dx11Ready = false;
    Project& project;
    Scene& scene;
    SceneRenderData gameRenderData;
    RecentProjects recent{std::filesystem::path("recent_projects.txt")};
    std::unique_ptr<HierarchyPanel> hierarchy;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<ContentPanel> content;
    std::unique_ptr<ViewportPanel> viewport;

    LuaRuntime scripts{scene};
    Scene editScene;
    bool playMode = false;
    bool focusGameView = false;
    std::string scriptError;
    std::string sceneError;

    bool saveScene() {
        return !project.isOpen() || project.saveScene(scene, sceneError);
    }

    void stopPlay() {
        if (!playMode) {
            return;
        }
        scripts.stop();
        scriptError = scripts.error();
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
        if (playMode || !project.isOpen()) {
            return;
        }
        if (!saveScene()) {
            return;
        }
        editScene = scene;
        scriptError.clear();
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
    bool openNewProjectModal = false;
    std::filesystem::path newProjectParent;
    char newProjectName[128] = {};

    bool openProject(const std::filesystem::path& folder) {
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
        if (!saveScene()) {
            return false;
        }
        project = std::move(opened);
        scene = std::move(loaded);
        scriptError.clear();
        if (hierarchy) { hierarchy->setSelectedIndex(-1); }
        if (viewport) { viewport->resetInteraction(); }
        recent.add(project.root());
        return true;
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

    m_impl->hierarchy = std::make_unique<HierarchyPanel>(m_impl->scene);
    m_impl->inspector = std::make_unique<InspectorPanel>(m_impl->scene, m_impl->project);
    m_impl->content = std::make_unique<ContentPanel>(m_impl->project, renderer);
    m_impl->viewport = std::make_unique<ViewportPanel>(m_impl->scene, renderer);

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
    m_impl->stopPlay();
    if (!m_impl->saveScene()) { std::cerr << m_impl->sceneError << '\n'; }
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
                    m_impl->newProjectParent = folder;
                    m_impl->newProjectName[0] = '\0';
                    m_impl->openNewProjectModal = true;
                }
            }
            if (ImGui::MenuItem("Open Project...")) {
                const std::filesystem::path folder =
                    openFolderDialog(m_impl->window, "Open Project Folder");
                if (!folder.empty()) {
                    m_impl->openProject(folder);
                }
            }
            if (ImGui::MenuItem("Save Scene", nullptr, false, m_impl->project.isOpen() && !m_impl->playMode)) {
                const bool saved = m_impl->saveScene();
                (void)saved; // Failure details appear in Scene Errors.
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

        if (ImGui::BeginMenu("Build")) {
            const bool building = m_impl->builder.status() == GameBuilder::Status::Building;
            const bool canBuild = m_impl->project.isOpen() && !m_impl->playMode && !building;
            if (ImGui::MenuItem("Build Standalone", nullptr, false, canBuild)) {
                const bool started = m_impl->saveScene() && m_impl->builder.start(m_impl->project.root());
                (void)started; // Failure details are displayed in Build Status.
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

    if (!m_impl->sceneError.empty()) {
        ImGui::Begin("Scene Errors");
        ImGui::TextWrapped("%s", m_impl->sceneError.c_str());
        ImGui::End();
    }

    if (!m_impl->scriptError.empty()) {
        ImGui::Begin("Lua Errors");
        ImGui::TextWrapped("%s", m_impl->scriptError.c_str());
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
            ImGui::DockBuilderFinish(dockspaceId);
        }
    }

    ImGui::End();

    ImGui::BeginDisabled(m_impl->playMode);
    if (m_impl->hierarchy) {
        m_impl->hierarchy->onImGuiRender(m_impl->playMode);
    }
    ImGui::EndDisabled();

    if (m_impl->viewport) {
        int selected = m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
        const bool focusGame = m_impl->focusGameView;
        m_impl->focusGameView = false;
        m_impl->viewport->onImGuiRender(selected, m_impl->playMode, focusGame);
        if (m_impl->hierarchy) {
            m_impl->hierarchy->setSelectedIndex(selected);
        }
    }

    ImGui::BeginDisabled(m_impl->playMode);
    if (m_impl->inspector) {
        const int selected =
            m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
        m_impl->inspector->onImGuiRender(selected);
    }
    ImGui::EndDisabled();

    if (m_impl->content) {
        m_impl->content->onImGuiRender();
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
            Project created;
            if (m_impl->saveScene() && created.create(
                    m_impl->newProjectParent, std::string(m_impl->newProjectName))) {
                Scene loaded;
                if (created.loadScene(loaded, m_impl->sceneError)) {
                    m_impl->project = std::move(created);
                    m_impl->scene = std::move(loaded);
                    m_impl->scriptError.clear();
                    if (m_impl->hierarchy) { m_impl->hierarchy->setSelectedIndex(-1); }
                    m_impl->recent.add(m_impl->project.root());
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

void UiLayer::endFrame() {
    if (!valid()) {
        return;
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

    std::vector<MeshInstance> meshInstances;
    std::vector<CameraFrustumInstance> frustumInstances;
    int selectedMesh = -1;
    const int selectedEntity =
        m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
    const float sceneAspect =
        (sceneViewHeight > 0)
            ? static_cast<float>(sceneViewWidth) / static_cast<float>(sceneViewHeight)
            : 1.0f;
    const auto& entities = m_impl->scene.entities();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].hasMesh) {
            MeshInstance instance{};
            instance.world = matrixFromTransform(entities[i].transform);
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
            frustum.position = entities[i].transform.position;
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
            sceneDraw.showTranslateGizmo = true;
            sceneDraw.translateGizmoOrigin = selected->transform.position;
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
