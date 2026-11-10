#include "LuaPackageConfig.hpp"

#include <lua.hpp>

#include <cstddef>

namespace sat::lua {
namespace {

// All lookup strings live on the Lua stack; errors may longjmp without skipping
// C++ destructors. require() receives a loader function and its virtual filename.
int embeddedSearcher(lua_State* state) {
    const char* module = luaL_checkstring(state, 1);
    const char* relative = luaL_gsub(state, module, ".", "/");
    lua_pushfstring(state, "%s.lua", relative);
    lua_pushvalue(state, -1);
    lua_gettable(state, lua_upvalueindex(1));
    if (lua_isnil(state, -1)) {
        lua_pop(state, 2);
        lua_pushfstring(state, "%s/init.lua", relative);
        lua_pushvalue(state, -1);
        lua_gettable(state, lua_upvalueindex(1));
    }
    if (lua_isnil(state, -1)) {
        lua_pushfstring(state, "\n\tno embedded Lua module '%s'", module);
        return 1;
    }
    std::size_t size = 0;
    const char* source = lua_tolstring(state, -1, &size);
    lua_pushfstring(state, "@Scripts/%s", lua_tostring(state, -2));
    if (luaL_loadbufferx(state, source, size, lua_tostring(state, -1), "t") != LUA_OK) {
        return lua_error(state);
    }
    lua_pushvalue(state, -2);
    return 2;
}

} // namespace

void configurePackagePath(lua_State* state, std::string_view modulePath) {
    lua_getglobal(state, "package");
    lua_pushlstring(state, modulePath.data(), modulePath.size());
    lua_setfield(state, -2, "path");
    lua_pop(state, 1);
}

void configureEmbeddedSearchers(lua_State* state, std::span<const LuaScriptSource> scripts) {
    lua_getglobal(state, "package");
    lua_pushliteral(state, "");
    lua_setfield(state, -2, "cpath");
    lua_getfield(state, -1, "searchers");
    lua_newtable(state);
    lua_rawgeti(state, -2, 1); // Retain Lua's preload searcher for built-ins.
    lua_rawseti(state, -2, 1);
    lua_newtable(state);
    for (const auto& script : scripts) {
        lua_pushlstring(state, script.name.data(), script.name.size());
        lua_pushlstring(state, script.source.data(), script.source.size());
        lua_rawset(state, -3);
    }
    lua_pushcclosure(state, embeddedSearcher, 1);
    lua_rawseti(state, -2, 2);
    lua_setfield(state, -3, "searchers");
    lua_pop(state, 2); // Original searchers, package.
}

} // namespace sat::lua
