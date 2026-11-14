#pragma once

#include "GameUi.hpp"

struct lua_State;

namespace sat::lua {

void registerUiBindings(lua_State* state, GameUi* ui, UiOwner owner, const Scene& scene);
[[nodiscard]] bool pushUiCallback(lua_State* state, const UiEvent& event);
void pruneUiCallbacks(lua_State* state, const GameUi& ui, UiOwner owner);

} // namespace sat::lua
