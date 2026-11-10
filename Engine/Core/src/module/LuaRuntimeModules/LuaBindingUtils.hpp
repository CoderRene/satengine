#pragma once

#include "Scene.hpp"

#include <lua.hpp>

#include <cmath>
#include <limits>

namespace sat::lua {

inline Scene& sceneFor(lua_State* state) {
    return *static_cast<Scene*>(lua_touserdata(state, lua_upvalueindex(1)));
}

inline Entity& checkedEntity(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    Scene& scene = sceneFor(state);
    Entity* entity = id > 0 ? scene.entityById(static_cast<EntityId>(id)) : nullptr;
    if (entity == nullptr) {
        luaL_error(state, "invalid or destroyed entity ID");
    }
    return *entity;
}

inline float checkedFloat(lua_State* state, int argument) {
    const lua_Number value = luaL_checknumber(state, argument);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        luaL_argerror(state, argument, "expected a finite float");
    }
    return static_cast<float>(value);
}

inline double checkedDouble(lua_State* state, int argument) {
    const lua_Number value = luaL_checknumber(state, argument);
    if (!std::isfinite(value)) {
        luaL_argerror(state, argument, "expected a finite number");
    }
    return static_cast<double>(value);
}

// Never allow C++ exceptions across the Lua C boundary. Lua errors are raised
// only after exception handlers (and their C++ objects) have been unwound.
template<int (*function)(lua_State*)>
int guarded(lua_State* state) {
    try {
        return function(state);
    } catch (...) {
    }
    return luaL_error(state, "engine binding failed (C++ exception)");
}

} // namespace sat::lua
