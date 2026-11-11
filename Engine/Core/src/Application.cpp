#include "Application.hpp"

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

    m_input = window.input();
    m_stopRequested = false;
    onStart();
    renderer.setAssetReader(m_assetReader);
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
        onUpdate(deltaSeconds);
        if (m_stopRequested) {
            break;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(window.nativeHandle()), &width, &height);
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
        renderer.present();
    }
    onStop();
    return 0;
}

} // namespace sat
