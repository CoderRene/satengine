#include "UiLayer.hpp"

#include "module/EditorSession.hpp"

#include "Renderer.hpp"
#include "Window.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_glfw.h>

#include <iostream>

namespace sat {

struct UiLayer::Impl : EditorSession {
    using EditorSession::EditorSession;
};

UiLayer::UiLayer(Window& window, Renderer& renderer, Scene& scene, Project& project)
    : m_impl(std::make_unique<Impl>(scene, project, window.input())) {
    m_impl->bind(window.nativeHandle(), renderer);
    if (!m_impl->windowBound()) {
        std::cerr << "Failed to start UI: window has no native handle\n";
        return;
    }
    m_impl->initialize();
}

UiLayer::~UiLayer() {
    m_impl->prepareClose();
}

bool UiLayer::valid() const {
    return m_impl->backendsReady() && m_impl->rendererValid();
}

void UiLayer::beginFrame() {
    if (!valid()) {
        return;
    }

    m_impl->pollSystems();
    m_impl->beginRendererFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    const bool canSaveScene = m_impl->canSaveScene();
    const ImGuiIO& io = ImGui::GetIO();
    if (canSaveScene && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        const bool saved = m_impl->saveScene();
        (void)saved; // Failure details appear in Scene Errors.
    }
    m_impl->tickScripts(io.DeltaTime);
    m_impl->drawInterface(canSaveScene);
}

bool UiLayer::requestClose() {
    return m_impl->requestClose();
}

void UiLayer::endFrame() {
    if (!valid()) {
        return;
    }

    m_impl->drawPrompts();
    m_impl->renderViews();
    ImGui::Render();
    m_impl->bindWindowTarget();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_impl->present();
    m_impl->paceEditor();
}

} // namespace sat
