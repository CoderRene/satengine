#include "EditorState.hpp"

#include "AssetLibrary.hpp"
#include "EditorLuaDefs.hpp"
#include "Input.hpp"
#include "LuaToolingSync.hpp"
#include "Renderer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <thread>
#include <utility>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace sat {

EditorSession::State::State(Scene& sceneIn, Project& projectIn, const Input& input)
    : project(projectIn), scene(sceneIn), play(input), savedSceneRevision(sceneIn.editRevision()) {}

EditorSession::EditorSession(Scene& scene, Project& project, const Input& input)
    : m_state(std::make_unique<State>(scene, project, input)) {
    if (project.isOpen()) { recordLoadedScene(); }
}

EditorSession::~EditorSession() = default;

void EditorSession::bind(void* nativeWindow, Renderer& renderer) {
    m_state->window = static_cast<GLFWwindow*>(nativeWindow);
    m_state->renderer = &renderer;
}

bool EditorSession::windowBound() const {
    return m_state->window != nullptr;
}

bool EditorSession::backendsReady() const {
    return m_state->glfwReady && m_state->dx11Ready;
}

bool EditorSession::rendererValid() const {
    return m_state->renderer != nullptr && m_state->renderer->valid();
}

void EditorSession::pollSystems() {
    m_state->builder.poll();
}

void EditorSession::beginRendererFrame() {
    m_state->frameStart = std::chrono::steady_clock::now();
    m_state->renderer->newFrame();
}

void EditorSession::bindWindowTarget() {
    m_state->renderer->bindWindowTarget();
}

void EditorSession::present() {
    m_state->renderer->present();
}

void EditorSession::paceEditor() {
    // Play mode stays uncapped so simulation can run as fast as the machine allows.
    if (m_state->play.active() || m_state->window == nullptr) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    bool keyDown = false;
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key))) {
            keyDown = true;
            break;
        }
    }
    const bool pointerActive = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] ||
        io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f || io.MouseDelta.x != 0.0f ||
        io.MouseDelta.y != 0.0f || ImGui::IsAnyItemActive() || keyDown;
    const bool sceneActive = m_state->viewport != nullptr && m_state->viewport->sceneViewInteracting();

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_state->window, &framebufferWidth, &framebufferHeight);
    const bool visible = glfwGetWindowAttrib(m_state->window, GLFW_FOCUSED) == GLFW_TRUE &&
        framebufferWidth >= 1 && framebufferHeight >= 1;

    const auto now = std::chrono::steady_clock::now();
    if (visible && (sceneActive || pointerActive)) {
        m_state->editActiveUntil = now + std::chrono::milliseconds(200);
    }
    // Scene navigation is 60 Hz. Idle and hidden editors tick slower.
    const bool fast = visible && (sceneActive || now < m_state->editActiveUntil);
    const auto period = !visible ? std::chrono::microseconds(100000)
        : (fast ? std::chrono::microseconds(1000000 / 60) : std::chrono::microseconds(1000000 / 30));
    const auto deadline = m_state->frameStart + period;
    if (deadline <= now) {
        return;
    }

    // sleep_for follows the ~15 ms system tick, so a 16 ms budget lands near 30 Hz.
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count();
    if (microseconds <= 0) {
        return;
    }
    static HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due{};
    due.QuadPart = -microseconds * 10;
    if (timer != nullptr && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
        return;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

bool EditorSession::canSaveScene() const {
    return m_state->project.isOpen() && !m_state->play.active() && !m_state->play.decisionPending();
}

bool EditorSession::hasUnsavedChanges() const {
    return m_state->scene.editRevision() != m_state->savedSceneRevision ||
        (m_state->project.isOpen() && !m_state->hasSavedScene);
}

void EditorSession::recordLoadedScene() {
    m_state->savedSceneRevision = m_state->scene.editRevision();
    std::error_code error;
    // Legacy projects without Scene.sat have not saved their default scene.
    m_state->hasSavedScene = std::filesystem::is_regular_file(m_state->project.root() / "Scene.sat", error);
}

bool EditorSession::saveScene() {
    if (!m_state->project.isOpen()) { return true; }
    if (!m_state->project.saveScene(m_state->scene, m_state->sceneError)) { return false; }
    m_state->savedSceneRevision = m_state->scene.editRevision();
    m_state->hasSavedScene = true;
    return true;
}

void EditorSession::tickScripts(float deltaSeconds) {
    if (m_state->play.active() && !m_state->play.update(deltaSeconds)) {
        stopPlay();
    }
}

void EditorSession::stopPlay() {
    if (!m_state->play.active()) {
        return;
    }
    m_state->play.stop(m_state->scene);
    if (m_state->viewport) {
        m_state->viewport->resetInteraction();
    }
}

void EditorSession::discardPlayEdits() {
    m_state->play.discardInto(m_state->scene);
    if (m_state->hierarchy) {
        m_state->hierarchy->setSelectedIndex(-1);
    }
}

void EditorSession::afterStoppingPlay(std::function<void()> action) {
    stopPlay();
    if (m_state->play.decisionPending()) {
        m_state->play.defer(std::move(action));
    } else {
        action();
    }
}

void EditorSession::startPlay() {
    m_state->assetImports->request([this] { startPlayNow(); });
}

void EditorSession::startPlayNow() {
    auto& state = *m_state;
    if (state.play.active() || state.play.decisionPending() || !state.project.isOpen()) {
        return;
    }
    if (state.viewport) { state.viewport->resetInteraction(); }
    if (!saveScene()) {
        return;
    }
    state.content->stopAudioPreview();
    if (state.viewport) { state.viewport->resetInteraction(); }
    if (!state.play.start(state.scene, state.project.root())) {
        if (state.viewport) { state.viewport->resetInteraction(); }
    }
}

void EditorSession::openProject(const std::filesystem::path& folder) {
    m_state->assetImports->request([this, folder] { openProjectNow(folder); });
}

void EditorSession::openProjectNow(const std::filesystem::path& folder) {
    Project opened;
    if (!opened.open(folder)) {
        m_state->recent.remove(folder);
        return;
    }
    if (m_state->project.isOpen() && opened.root() == m_state->project.root()) {
        return;
    }
    Scene loaded;
    if (!opened.loadScene(loaded, m_state->sceneError)) {
        return;
    }
    afterStoppingPlay([this, opened = std::move(opened), loaded = std::move(loaded)]() mutable {
        auto& state = *m_state;
        if (state.viewport) { state.viewport->resetInteraction(); }
        if (!saveScene()) {
            return;
        }
        state.assetImports->clear();
        state.content->setSelectedAsset({});
        state.project = std::move(opened);
        state.scene = std::move(loaded);
        recordLoadedScene();
        state.renderer->setAssetReader(fileAssetReader(state.project.root()));
        state.renderer->setProjectShaderSource(fileAssetReader(state.project.root()));
        state.play.setAssetReader(fileAssetReader(state.project.root()));
        if (state.hierarchy) { state.hierarchy->setSelectedIndex(-1); }
        if (state.viewport) { state.viewport->resetInteraction(); }
        state.recent.add(state.project.root());
        syncLuaTooling();
    });
}

void EditorSession::reloadAssets() {
    m_state->assetImports->request([this] {
        m_state->renderer->setAssetReader(fileAssetReader(m_state->project.root()));
        m_state->renderer->setProjectShaderSource(fileAssetReader(m_state->project.root()));
        if (m_state->assetImports->active()) { m_state->assetImports->select(m_state->assetImports->path()); }
    });
}

void EditorSession::selectEntity(EntityId id) {
    m_state->assetImports->request([this, id] {
        m_state->viewport->resetInteraction();
        m_state->assetImports->clear();
        m_state->content->setSelectedAsset({});
        const auto index = m_state->scene.entityIndex(id);
        m_state->hierarchy->setSelectedIndex(index ? static_cast<int>(*index) : -1);
    });
}

void EditorSession::syncLuaTooling() const {
    const auto defs = editorLuaDefsDirectory();
    if (defs.empty() || !syncProjectLuaTooling(m_state->project.root(), defs)) {
        std::cerr << "Warning: failed to sync LuaDefs into the project (Cursor linting may not work)\n";
    }
}

bool EditorSession::createProjectFromDialog() {
    auto& state = *m_state;
    if (state.viewport) { state.viewport->resetInteraction(); }
    Project created;
    if (!saveScene() || !created.create(state.newProjectParent, std::string(state.newProjectName))) {
        return false;
    }
    Scene loaded;
    if (!created.loadScene(loaded, state.sceneError)) {
        return false;
    }
    state.assetImports->clear();
    state.content->setSelectedAsset({});
    state.project = std::move(created);
    state.play.setAssetReader(fileAssetReader(state.project.root()));
    state.scene = std::move(loaded);
    recordLoadedScene();
    reloadAssets();
    if (state.hierarchy) { state.hierarchy->setSelectedIndex(-1); }
    state.recent.add(state.project.root());
    syncLuaTooling();
    return true;
}

bool EditorSession::createProjectThunk(void* context) {
    return static_cast<EditorSession*>(context)->createProjectFromDialog();
}

void EditorSession::prepareClose() {
    if (m_state->viewport) { m_state->viewport->resetInteraction(); }
    stopPlay();
    if (m_state->play.decisionPending()) { discardPlayEdits(); }
    if (!m_state->discardSceneOnClose && !saveScene()) {
        std::cerr << m_state->sceneError << '\n';
    }
    shutdown();
}

} // namespace sat
