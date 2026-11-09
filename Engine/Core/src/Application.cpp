#include "Application.hpp"

#include "Renderer.hpp"
#include "UiLayer.hpp"
#include "Window.hpp"

#include <iostream>

namespace sat {

int Application::run() {
    Window window(1280, 720, "SatEngine");
    Renderer renderer(window.nativeHandle());
    UiLayer ui(window, renderer);

    onStart();
    while (!window.shouldClose()) {
        window.pollEvents();
        ui.beginFrame();
        onUpdate();
        ui.endFrame();
    }
    onStop();

    return 0;
}

void Application::onStart() {
    std::cout << "Application started\n";
}

void Application::onUpdate() {
}

void Application::onStop() {
    std::cout << "Application stopped\n";
}

} // namespace sat
