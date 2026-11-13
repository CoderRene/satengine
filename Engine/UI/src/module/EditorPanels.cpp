#include "EditorState.hpp"

#include <imgui.h>

namespace sat {

void EditorSession::drawPanels() {
    auto& state = *m_state;
    const bool sceneReadOnly = state.play.decisionPending();
    const bool playing = state.play.active();
    if (state.hierarchy) {
        const int previous = state.hierarchy->selectedIndex();
        state.hierarchy->onImGuiRender(sceneReadOnly);
        if (state.hierarchy->selectionActivated()) {
            const int selected = state.hierarchy->selectedIndex();
            const Entity* entity = selected >= 0 ? state.scene.entity(static_cast<std::size_t>(selected)) : nullptr;
            const EntityId id = entity ? entity->id() : kInvalidEntityId;
            state.hierarchy->setSelectedIndex(previous);
            selectEntity(id);
        }
    }

    if (state.viewport) {
        int selected = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
        const bool focusGame = state.play.takeFocusGame();
        state.viewport->onImGuiRender(selected, state.play.viewScene(state.scene), sceneReadOnly, focusGame);
        if (state.hierarchy && state.viewport->selectionActivated()) {
            const Entity* entity = selected >= 0 ? state.scene.entity(static_cast<std::size_t>(selected)) : nullptr;
            const EntityId id = entity ? entity->id() : kInvalidEntityId;
            selectEntity(id);
        }
    }

    ImGui::BeginDisabled(sceneReadOnly);
    if (state.assetImports->active()) {
        state.assetImports->draw(playing);
    } else if (state.inspector) {
        const int selected = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
        const Entity* authored = selected >= 0 ? state.scene.entity(static_cast<std::size_t>(selected)) : nullptr;
        Entity* live = playing && authored ? state.play.game().entityById(authored->id()) : nullptr;
        state.inspector->onImGuiRender(selected, live);
    }
    ImGui::EndDisabled();

    if (state.content) {
        state.content->onImGuiRender(playing);
    }

    if (state.luaLog) {
        const bool focusLog = state.play.takeFocusLog();
        state.luaLog->onImGuiRender(focusLog);
    }
}

} // namespace sat
