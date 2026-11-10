#pragma once

#include "LuaRuntime.hpp"

#include <string_view>

struct lua_State;

namespace sat::lua {

// Assigns package.path from project data (never treated as Lua code).
void configurePackagePath(lua_State* state, std::string_view modulePath);

// Restricts package.cpath and installs an embedded Scripts/ searcher after preload.
void configureEmbeddedSearchers(lua_State* state, std::span<const LuaScriptSource> scripts);

} // namespace sat::lua
