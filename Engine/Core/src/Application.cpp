#include "Application.hpp"

#include "GameUi.hpp"
#include "Renderer.hpp"
#include "SceneRenderData.hpp"
#include "Window.hpp"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>

namespace sat {

int Application::run() {
    Window window(1280, 720, "SatGame");
    if (window.nativeHandle() == nullptr) {
        return 1;
    }
    Renderer renderer(window.nativeHandle());
    if (!renderer.valid()) {
        return 1;
    }

    GameUi ui(renderer.uiRenderInterface(),
        [this](std::string_view path, std::string& bytes, std::string& error) {
            return m_assetReader ? m_assetReader(path, bytes, error) : AssetReadResult::Missing;
        });
    struct ResetUi {
        GameUi*& pointer;
        ~ResetUi() { pointer = nullptr; }
    } reset{m_gameUi};
    m_gameUi = &ui;

    m_input = window.input();
    m_stopRequested = false;
    onStart();
    renderer.setAssetReader(m_assetReader);
    renderer.setShaderReader(m_shaderReader);
    if (!m_stopRequested && !renderer.initializeShaders()) {
        onStop();
        return 1;
    }
    SceneRenderData renderData;
    std::size_t reportedAssetErrors = 0;
    auto previous = std::chrono::steady_clock::now();
    while (!m_stopRequested && !window.shouldClose()) {
        window.pollEvents();
        m_input = window.input();
        if (window.shouldClose()) {
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - previous).count();
        previous = now;
        int width = 0;
        int height = 0;
        auto* nativeWindow = static_cast<GLFWwindow*>(window.nativeHandle());
        glfwGetFramebufferSize(nativeWindow, &width, &height);
        int clientWidth = 0;
        int clientHeight = 0;
        glfwGetWindowSize(nativeWindow, &clientWidth, &clientHeight);
        const bool visible = width > 0 && height > 0 && clientWidth > 0 && clientHeight > 0;
        const bool focused = visible && glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) == GLFW_TRUE;
        ui.setViewport(width, height);
        ui.processInput(m_input, 0.0f, 0.0f,
            visible ? static_cast<float>(width) / static_cast<float>(clientWidth) : 1.0f,
            visible ? static_cast<float>(height) / static_cast<float>(clientHeight) : 1.0f,
            focused, focused);
        onUpdate(deltaSeconds);
        if (m_stopRequested) { break; }
        ui.update();
        if (width < 1 || height < 1) {
            // Avoid rendering/presenting a minimized window or spinning at full speed.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        renderer.newFrame();
        renderData.update(m_scene, width, height);
        if (!renderer.renderWindow(renderData.camera(), renderData.drawList())) {
            onStop();
            return 1;
        }
        const auto& assetErrors = renderer.assetErrors();
        for (; reportedAssetErrors < assetErrors.size(); ++reportedAssetErrors) {
            std::cerr << assetErrors[reportedAssetErrors] << '\n';
        }
        renderer.renderUi(ui, UiTarget::Window);
        renderer.present();
    }
    onStop();
    return 0;
}

} // namespace sat
