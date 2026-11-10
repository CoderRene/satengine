#include "LuaLogBindings.hpp"

#include "LuaBindingUtils.hpp"
#include "ScriptLog.hpp"

#include <lua.hpp>

#include <iostream>
#include <string>

namespace sat::lua {
namespace {

ScriptLog* logFor(lua_State* state) {
    return static_cast<ScriptLog*>(lua_touserdata(state, lua_upvalueindex(1)));
}

std::string joinArgs(lua_State* state) {
    const int count = lua_gettop(state);
    std::string message;
    for (int index = 1; index <= count; ++index) {
        if (index > 1) {
            message.push_back('\t');
        }
        std::size_t length = 0;
        const char* text = luaL_tolstring(state, index, &length);
        message.append(text, length);
        lua_pop(state, 1);
    }
    return message;
}

int logInfo(lua_State* state) {
    const std::string message = joinArgs(state);
    if (ScriptLog* log = logFor(state); log != nullptr) {
        log->info(message);
    } else {
        std::clog << "[log] " << message << '\n';
    }
    return 0;
}

int logWarn(lua_State* state) {
    const std::string message = joinArgs(state);
    if (ScriptLog* log = logFor(state); log != nullptr) {
        log->warn(message);
    } else {
        std::cerr << "[warn] " << message << '\n';
    }
    return 0;
}

} // namespace

void registerLogBindings(lua_State* state, ScriptLog* log) {
    const luaL_Reg bindings[] = {
        // @lua fun sat.log(...: any)
        {"log", guarded<logInfo>},
        // @lua fun sat.warn(...: any)
        {"warn", guarded<logWarn>},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, log);
    luaL_setfuncs(state, bindings, 1);
}

} // namespace sat::lua