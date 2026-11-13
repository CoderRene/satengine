#include "EditorApplication.hpp"

#include "EnginePaths.hpp"
#include "Renderer.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "UiLayer.hpp"
#include "Window.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <iterator>

namespace sat {

int EditorApplication::run() {
    Window window(1280, 720, "SatEngine");
    if (window.nativeHandle() == nullptr) {
        return 1;
    }
    window.setIcon("SatEngine_ic.png");
    window.setIcon(SAT_ENGINE_ASSETS_DIR "/SatEngine_ic.png");
    Renderer renderer(window.nativeHandle());
    if (!renderer.valid()) {
        return 1;
    }
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (length > 0 && length < std::size(executable)) {
        renderer.setShaderReader(fileShaderReader(std::filesystem::path(executable).parent_path()));
    }
    Scene scene;
    Project project;
    UiLayer ui(window, renderer, scene, project);
    if (!ui.valid()) {
        return 1;
    }
    while (true) {
        window.pollEvents();
        if (window.shouldClose()) {
            if (ui.requestClose()) { break; }
            glfwSetWindowShouldClose(static_cast<GLFWwindow*>(window.nativeHandle()), GLFW_FALSE);
        }
        ui.beginFrame();
        ui.endFrame();
    }
    return 0;
}

} // namespace sat
