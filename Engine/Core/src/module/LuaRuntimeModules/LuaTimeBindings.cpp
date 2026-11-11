#include "LuaTimeBindings.hpp"

#include <lua.hpp>

#include <cmath>
#include <limits>

namespace sat::lua {
namespace {
TimeState& timeFor(lua_State* state) {
    return *static_cast<TimeState*>(lua_touserdata(state, lua_upvalueindex(1)));
}

int deltaSeconds(lua_State* state) {
    lua_pushnumber(state, timeFor(state).m_deltaSeconds);
    return 1;
}

int unscaledDeltaSeconds(lua_State* state) {
    lua_pushnumber(state, timeFor(state).m_unscaledDeltaSeconds);
    return 1;
}

int elapsedSeconds(lua_State* state) {
    lua_pushnumber(state, timeFor(state).m_elapsedSeconds);
    return 1;
}

int unscaledElapsedSeconds(lua_State* state) {
    lua_pushnumber(state, timeFor(state).m_unscaledElapsedSeconds);
    return 1;
}

int timeScale(lua_State* state) {
    lua_pushnumber(state, timeFor(state).m_timeScale);
    return 1;
}

int setTimeScale(lua_State* state) {
    const lua_Number value = luaL_checknumber(state, 1);
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<float>::max()) {
        return luaL_argerror(state, 1, "time scale must be finite, nonnegative and representable as float");
    }
    const float scale = static_cast<float>(value);
    if (value > 0 && scale == 0.0f) {
        return luaL_argerror(state, 1, "time scale is too small to represent as float");
    }
    timeFor(state).m_timeScale = scale;
    return 0;
}
} // namespace

void registerTimeBindings(lua_State* state, TimeState& time) {
    lua_newtable(state);
    const luaL_Reg bindings[] = {
        // @lua fun sat.time.deltaSeconds(): number
        {"deltaSeconds", deltaSeconds},
        // @lua fun sat.time.unscaledDeltaSeconds(): number
        {"unscaledDeltaSeconds", unscaledDeltaSeconds},
        // @lua fun sat.time.elapsedSeconds(): number
        {"elapsedSeconds", elapsedSeconds},
        // @lua fun sat.time.unscaledElapsedSeconds(): number
        {"unscaledElapsedSeconds", unscaledElapsedSeconds},
        // @lua fun sat.time.timeScale(): number
        {"timeScale", timeScale},
        // @lua fun sat.time.setTimeScale(scale: number)
        {"setTimeScale", setTimeScale},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &time);
    luaL_setfuncs(state, bindings, 1);
    lua_setfield(state, -2, "time");
}

} // namespace sat::lua
