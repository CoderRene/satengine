#include "EditorState.hpp"
#include "AnimationAsset.hpp"

#include "EditorTheme.hpp"

#include "AssetData.hpp"
#include "AssetLibrary.hpp"
#include "EnginePaths.hpp"
#include "Renderer.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_glfw.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace sat {

void EditorSession::initialize() {
    auto& state = *m_state;
    Renderer& renderer = *state.renderer;
    renderer.setProjectAssetRoot(state.project.root());
    renderer.setProjectShaderSource(fileAssetReader(state.project.root()));
    state.play.setAssetReader(fileAssetReader(state.project.root()));
    state.previewAnimation.setAssetReader(fileAssetReader(state.project.root()));
    state.hierarchy = std::make_unique<HierarchyPanel>(state.scene, &state.project);
    state.inspector = std::make_unique<InspectorPanel>(state.scene, state.project);
    state.assetImports = std::make_unique<AssetImportPanel>(state.project, renderer);
    state.content = std::make_unique<ContentPanel>(state.project, renderer);
    state.content->setMoveHandler([this](std::string_view oldPath, std::string_view newPath, bool directory) {
        resetViewportInteraction();
        const auto replace = [directory](std::string& path, std::string_view from, std::string_view to) {
            if (path != from && !(directory && path.starts_with(from) &&
                    path.size() > from.size() && path[from.size()] == '/')) { return false; }
            path.replace(0, from.size(), to);
            return true;
        };
        const auto updateEntity = [&](Entity& entity) {
            bool changed = replace(entity.mesh.meshPath, oldPath, newPath);
            changed |= replace(entity.mesh.materialPath, oldPath, newPath);
            changed |= replace(entity.animator.controllerPath, oldPath, newPath);
            if (isAnimationClipAssetPath(entity.animator.clip)) {
                changed |= replace(entity.animator.clip, oldPath, newPath);
            }
            changed |= replace(entity.audioSource.clipPath, oldPath, newPath);
            if (entity.gameUi) {
                changed |= replace(entity.gameUi->documentPath, oldPath, newPath);
                changed |= replace(entity.gameUi->fontPath, oldPath, newPath);
            }
            if (oldPath.starts_with("Scripts/") && newPath.starts_with("Scripts/")) {
                for (auto& script : entity.scripts) {
                    changed |= replace(script.path, oldPath.substr(8), newPath.substr(8));
                }
            }
            return changed;
        };
        const auto updateLighting = [&](LightingSettings& settings) {
            return replace(settings.environmentPath, oldPath, newPath);
        };
        m_state->history.relocateReferences(m_state->scene, updateEntity, updateLighting);
        auto error = m_state->assetImports->updateMaterialReferences(oldPath, newPath, directory);
        m_state->animationGraph->relocate(oldPath, newPath, directory);
        if (isShaderAssetPath(oldPath) || (directory && oldPath.starts_with("Assets/"))) {
            m_state->renderer->setProjectShaderSource(fileAssetReader(m_state->project.root()));
        }
        if (m_state->assetImports->active()) {
            const std::string_view selected = m_state->assetImports->path();
            if (selected != oldPath && !(directory && selected.starts_with(oldPath) &&
                    selected.size() > oldPath.size() && selected[oldPath.size()] == '/')) {
                m_state->assetImports->select(std::string(selected));
            }
        }
        return error;
    });
    state.content->setSelectionHandler([this](std::string path) {
        if (m_state->assetImports->active() && m_state->assetImports->path() == path) { return; }
        m_state->assetImports->request([this, path = std::move(path)] {
            resetViewportInteraction();
            m_state->hierarchy->setSelectedIndex(-1);
            if (isAnimationGraphAssetPath(path)) {
                m_state->animationGraph->open(path);
                m_state->assetImports->clear();
                m_state->content->setSelectedAsset(path);
            } else if (isAudioAssetPath(path)) {
                m_state->assetImports->clear();
                m_state->content->setSelectedAsset(path);
            } else if (isMeshAssetPath(path) || isTextureAssetPath(path) || isMaterialAssetPath(path) || isAnimationClipAssetPath(path)) {
                m_state->assetImports->select(path);
                m_state->content->setSelectedAsset(path);
            } else {
                m_state->assetImports->clear();
                m_state->content->setSelectedAsset({});
            }
        });
    });
    state.animationGraph = std::make_unique<AnimationGraphPanel>(state.project, renderer);
    state.viewport = std::make_unique<ViewportPanel>(state.scene, renderer, *state.window);
    state.luaLog = std::make_unique<LuaLogPanel>(state.play.log());

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

    if (!ImGui_ImplGlfw_InitForOther(state.window, true)) {
        std::cerr << "Failed to start UI: GLFW backend\n";
        return;
    }
    state.glfwReady = true;
    state.dx11Ready = ImGui_ImplDX11_Init(
        static_cast<ID3D11Device*>(renderer.nativeDevice()),
        static_cast<ID3D11DeviceContext*>(renderer.nativeContext()));
    if (!state.dx11Ready) {
        std::cerr << "Failed to start UI: Direct3D 11 backend\n";
    }
}

void EditorSession::shutdown() {
    if (m_state->dx11Ready) {
        ImGui_ImplDX11_Shutdown();
    }
    if (m_state->glfwReady) {
        ImGui_ImplGlfw_Shutdown();
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        m_state->animationGraph.reset();
        ImGui::DestroyContext();
    }
}

} // namespace sat
