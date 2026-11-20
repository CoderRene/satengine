#include "EditorState.hpp"

#include "Window.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <span>

namespace sat {

void EditorSession::drawPanels() {
    auto& state = *m_state;
    const bool sceneReadOnly = state.play.decisionPending();
    const bool playing = state.play.active();
    const bool recording = state.recordsSceneHistory();
    const bool transformActive = state.viewport && state.viewport->transformActive();
    if (state.hierarchy) {
        const auto before = recording ? state.sceneSelection() : SceneEditHistory::Selection{};
        const int previousIndex = state.hierarchy->selectedIndex();
        const Entity* previous = previousIndex >= 0 ? state.scene.entity(static_cast<std::size_t>(previousIndex)) : nullptr;
        const EntityId previousId = previous ? previous->id() : kInvalidEntityId;
        state.hierarchy->onImGuiRender(sceneReadOnly || transformActive);
        if (state.hierarchy->selectionActivated()) {
            const int selected = state.hierarchy->selectedIndex();
            const Entity* entity = selected >= 0 ? state.scene.entity(static_cast<std::size_t>(selected)) : nullptr;
            const EntityId id = entity ? entity->id() : kInvalidEntityId;
            if (state.assetImports->dirty()) {
                const auto index = state.scene.entityIndex(previousId);
                state.hierarchy->setSelectedIndex(index ? static_cast<int>(*index) : -1);
                selectEntity(id);
            } else {
                selectEntity(id, true);
            }
        }
        if (recording && state.history.needsCapture(state.scene) && (!state.viewport || !state.viewport->transformActive())) {
            const auto after = state.sceneSelection();
            state.history.capture(state.scene, "Hierarchy Edit", 0, std::nullopt, &before, &after);
        }
    }

    if (state.viewport && state.hierarchy) {
        int selected = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
        const bool focusGame = state.play.takeFocusGame();
        state.viewport->onImGuiRender(selected, state.hierarchy->selectedIds(), state.play.viewScene(state.scene), sceneReadOnly, focusGame,
            playing ? state.gameUi.get() : nullptr, &state.input);
        if (recording) {
            if (transformActive && !state.viewport->transformActive()) {
                state.history.capture(state.scene, "Transform Selection", 0, state.viewport->transformTargets());
            }
        }
        if (state.viewport->deleteRequested() && !state.viewport->transformActive()) {
            const auto before = recording ? state.sceneSelection() : SceneEditHistory::Selection{};
            state.hierarchy->deleteSelection();
            state.viewport->resetInteraction();
            if (recording && state.history.needsCapture(state.scene)) {
                const auto after = state.sceneSelection();
                state.history.capture(state.scene, "Delete Selection", 0, std::nullopt, &before, &after);
            }
        } else if (state.viewport->selectionActivated()) {
            const Entity* entity = selected >= 0 ? state.scene.entity(static_cast<std::size_t>(selected)) : nullptr;
            const EntityId id = entity ? entity->id() : kInvalidEntityId;
            selectEntity(id, false, state.viewport->selectionToggle());
        }
    }

    ImGui::BeginDisabled(sceneReadOnly || (state.viewport && state.viewport->transformActive()));
    if (state.assetImports->active()) {
        ImGui::BeginDisabled(state.content && state.content->deletingFiles());
        state.assetImports->draw(playing);
        ImGui::EndDisabled();
    } else if (state.inspector) {
        const int selected = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
        const Entity* authored = selected >= 0 ? state.scene.entity(static_cast<std::size_t>(selected)) : nullptr;
        Entity* live = playing && authored ? state.play.game().entityById(authored->id()) : nullptr;
        const auto inspectorRevision = state.scene.editRevision();
        const EntityId id = authored ? authored->id() : kInvalidEntityId;
        state.inspector->onImGuiRender(selected, live, state.content.get(), playing ? &state.play.animation() : &state.previewAnimation, state.animationGraph.get());
        if (recording && id != kInvalidEntityId && state.scene.editRevision() != inspectorRevision) {
            auto& ids = state.inspectorEditIds;
            ids.clear();
            ids.push_back(id);
            const Scene& before = state.history.baseline();
            const auto* current = state.scene.entityById(id);
            const auto* previous = before.entityById(id);
            if (current && previous && current->hasChildren() && current->transform != previous->transform) {
                for (const auto& item : state.scene.entities()) {
                    EntityId parent = item.parentId();
                    while (parent != id && parent != kInvalidEntityId) {
                        const auto* ancestor = state.scene.entityById(parent);
                        parent = ancestor ? ancestor->parentId() : kInvalidEntityId;
                    }
                    if (parent == id) { ids.push_back(item.id()); }
                }
            }
            if (current && previous && current->hasCamera && current->camera.isMain &&
                (!previous->hasCamera || !previous->camera.isMain)) {
                const int oldMain = before.mainCameraIndex();
                if (oldMain >= 0) { ids.push_back(before.entity(static_cast<std::size_t>(oldMain))->id()); }
            }
            const auto active = ImGui::GetActiveID();
            const auto gesture = active != 0 ? active : ImGui::GetCurrentContext()->DeactivatedItemData.ID;
            state.history.capture(state.scene, "Inspector Edit", gesture, std::span<const EntityId>(ids));
            if (active == 0) { state.history.finishGesture(); }
        }
    }
    ImGui::EndDisabled();

    if (state.content) {
        const auto drops = sceneReadOnly ? std::span<const FileDrop>{} : state.platformWindow->fileDrops();
        state.content->onImGuiRender(playing, drops,
            !sceneReadOnly && !state.assetImports->dirty() && (!state.viewport || !state.viewport->transformActive()));

    }

    if (recording && (!state.viewport || !state.viewport->transformActive()) && ImGui::GetActiveID() == 0) { state.history.finishGesture(); }

    const int animationIndex = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
    const auto* animationEntity = animationIndex >= 0 ? state.scene.entity(static_cast<std::size_t>(animationIndex)) : nullptr;
    if (state.animationGraph) {
        state.animationGraph->draw(playing ? &state.play.animation() : &state.previewAnimation,
            animationEntity ? animationEntity->id() : kInvalidEntityId);
    }
    if (state.luaLog) {
        const bool focusLog = state.play.takeFocusLog();
        state.luaLog->onImGuiRender(focusLog);
    }
}

} // namespace sat
