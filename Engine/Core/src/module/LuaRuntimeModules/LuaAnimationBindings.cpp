#include "LuaAnimationBindings.hpp"
#include "AnimationSystem.hpp"
#include "LuaBindingUtils.hpp"
#include <cstdint>
#include <string_view>

namespace sat::lua {
namespace {
AnimationSystem& system(lua_State* state) {
    auto* value = static_cast<AnimationSystem*>(lua_touserdata(state, lua_upvalueindex(2)));
    if (!value) { luaL_error(state, "animation system is unavailable"); }
    return *value;
}
std::string_view string(lua_State* state, int index) {
    std::size_t size = 0;
    const char* value = luaL_checklstring(state, index, &size);
    return {value, size};
}
int play(lua_State* state) {
    auto& animation = system(state);
    const auto id = checkedEntity(state).id();
    const auto clip = string(state, 2);
    bool loop = true;
    float speed = 1, fade = 0.2f;
    if (!lua_isnoneornil(state, 3)) {
        luaL_checktype(state, 3, LUA_TTABLE);
        lua_getfield(state, 3, "loop"); if (!lua_isnil(state, -1)) { loop = lua_toboolean(state, -1) != 0; } lua_pop(state, 1);
        lua_getfield(state, 3, "speed"); if (!lua_isnil(state, -1)) { speed = checkedFloat(state, -1); } lua_pop(state, 1);
        lua_getfield(state, 3, "fade"); if (!lua_isnil(state, -1)) { fade = checkedFloat(state, -1); } lua_pop(state, 1);
    }
    const bool success = animation.play(id, clip, loop, fade) && animation.setSpeed(id, speed);
    lua_pushboolean(state, success); return 1;
}
int stop(lua_State* state) { system(state).stop(checkedEntity(state).id()); return 0; }
int time(lua_State* state) { lua_pushnumber(state, system(state).time(checkedEntity(state).id())); return 1; }
int isPlaying(lua_State* state) { lua_pushboolean(state, system(state).isPlaying(checkedEntity(state).id())); return 1; }
int setSpeed(lua_State* state) { lua_pushboolean(state, system(state).setSpeed(checkedEntity(state).id(), checkedFloat(state, 2))); return 1; }
int setTime(lua_State* state) { lua_pushboolean(state, system(state).setTime(checkedEntity(state).id(), checkedFloat(state, 2))); return 1; }
int setBool(lua_State* state) {
    luaL_checktype(state, 3, LUA_TBOOLEAN);
    lua_pushboolean(state, system(state).setBool(checkedEntity(state).id(), string(state, 2), lua_toboolean(state, 3) != 0)); return 1;
}
int setFloat(lua_State* state) { lua_pushboolean(state, system(state).setFloat(checkedEntity(state).id(), string(state, 2), checkedFloat(state, 3))); return 1; }
int setInt(lua_State* state) {
    const auto value = luaL_checkinteger(state, 3);
    if (value < INT32_MIN || value > INT32_MAX) { return luaL_argerror(state, 3, "expected int32"); }
    lua_pushboolean(state, system(state).setInt(checkedEntity(state).id(), string(state, 2), static_cast<std::int32_t>(value))); return 1;
}
int setTrigger(lua_State* state) { lua_pushboolean(state, system(state).setTrigger(checkedEntity(state).id(), string(state, 2))); return 1; }
int currentState(lua_State* state) {
    const auto name = system(state).currentState(checkedEntity(state).id()); lua_pushlstring(state, name.data(), name.size()); return 1;
}
int crossfadeTo(lua_State* state) {
    lua_pushboolean(state, system(state).crossfadeTo(checkedEntity(state).id(), string(state, 2), lua_isnoneornil(state, 3) ? 0.2f : checkedFloat(state, 3))); return 1;
}
int clips(lua_State* state) {
    const auto names = system(state).clipNames(checkedEntity(state).mesh.meshPath);
    lua_createtable(state, static_cast<int>(names.size()), 0);
    for (std::size_t index = 0; index < names.size(); ++index) { lua_pushlstring(state, names[index].data(), names[index].size()); lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1)); }
    return 1;
}
}
void registerAnimationBindings(lua_State* state, Scene& scene, AnimationSystem* animation) {
    lua_newtable(state);
    const luaL_Reg bindings[] = {
        // @lua fun sat.animation.play(entity: EntityId, clip: string, options?: table): boolean
        {"play", guarded<play>},
        // @lua fun sat.animation.stop(entity: EntityId)
        {"stop", guarded<stop>},
        // @lua fun sat.animation.time(entity: EntityId): number
        {"time", guarded<time>},
        // @lua fun sat.animation.isPlaying(entity: EntityId): boolean
        {"isPlaying", guarded<isPlaying>},
        // @lua fun sat.animation.setSpeed(entity: EntityId, speed: number): boolean
        {"setSpeed", guarded<setSpeed>},
        // @lua fun sat.animation.setTime(entity: EntityId, seconds: number): boolean
        {"setTime", guarded<setTime>},
        // @lua fun sat.animation.setBool(entity: EntityId, name: string, value: boolean): boolean
        {"setBool", guarded<setBool>},
        // @lua fun sat.animation.setFloat(entity: EntityId, name: string, value: number): boolean
        {"setFloat", guarded<setFloat>},
        // @lua fun sat.animation.setInt(entity: EntityId, name: string, value: integer): boolean
        {"setInt", guarded<setInt>},
        // @lua fun sat.animation.setTrigger(entity: EntityId, name: string): boolean
        {"setTrigger", guarded<setTrigger>},
        // @lua fun sat.animation.currentState(entity: EntityId): string
        {"currentState", guarded<currentState>},
        // @lua fun sat.animation.crossfadeTo(entity: EntityId, state: string, seconds?: number): boolean
        {"crossfadeTo", guarded<crossfadeTo>},
        // @lua fun sat.animation.clips(entity: EntityId): string[]
        {"clips", guarded<clips>},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &scene); lua_pushlightuserdata(state, animation);
    luaL_setfuncs(state, bindings, 2); lua_setfield(state, -2, "animation");
}
}
