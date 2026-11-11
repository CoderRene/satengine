#include "LuaMessageBindings.hpp"

#include "LuaBindingUtils.hpp"

#include <lua.hpp>

#include <cmath>
#include <string_view>
#include <utility>

namespace sat::lua {
namespace {
constexpr std::size_t kMaxMessages = 1024;
constexpr std::size_t kMaxPendingBytes = 1024 * 1024;
constexpr std::size_t kMaxMessageNameBytes = 256;

int sendMessage(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    std::size_t pathSize = 0;
    const char* path = luaL_checklstring(state, 2, &pathSize);
    std::size_t nameSize = 0;
    const char* name = luaL_checklstring(state, 3, &nameSize);
    if (!isValidScriptPath(std::string_view(path, pathSize))) {
        return luaL_argerror(state, 2, "expected a script path relative to Scripts/");
    }
    if (nameSize == 0 || nameSize > kMaxMessageNameBytes) {
        return luaL_argerror(state, 3, "message name must contain 1 to 256 bytes");
    }
    const int payloadType = lua_type(state, 4);
    std::size_t payloadSize = 0;
    const char* payloadString = nullptr;
    double payloadNumber = 0;
    if (payloadType == LUA_TSTRING) {
        payloadString = lua_tolstring(state, 4, &payloadSize);
    } else if (payloadType == LUA_TNUMBER) {
        payloadNumber = lua_tonumber(state, 4);
        if (!std::isfinite(payloadNumber)) {
            return luaL_argerror(state, 4, "message number must be finite");
        }
    } else if (payloadType != LUA_TNONE && payloadType != LUA_TNIL && payloadType != LUA_TBOOLEAN) {
        return luaL_argerror(state, 4, "message payload must be nil, boolean, number or string");
    }
    auto& bus = *static_cast<MessageBus*>(lua_touserdata(state, lua_upvalueindex(1)));
    auto& scene = *static_cast<Scene*>(lua_touserdata(state, lua_upvalueindex(2)));
    const Entity* entity = id > 0 ? scene.entityById(static_cast<EntityId>(id)) : nullptr;
    if (!bus.m_accepting || entity == nullptr) {
        lua_pushboolean(state, false);
        return 1;
    }
    std::size_t targetIndex = 0;
    for (; targetIndex < bus.m_targets.size(); ++targetIndex) {
        const auto& target = bus.m_targets[targetIndex];
        if (target.m_entity == entity->id() && target.m_path == std::string_view(path, pathSize) && target.m_accepting) {
            break;
        }
    }
    bool enabled = false;
    for (const auto& script : entity->scripts) {
        if (script.path == std::string_view(path, pathSize)) {
            enabled = script.enabled;
            break;
        }
    }
    if (targetIndex == bus.m_targets.size() || !enabled) {
        lua_pushboolean(state, false);
        return 1;
    }
    if (bus.m_pending.size() >= kMaxMessages || payloadSize > kMaxPendingBytes ||
        nameSize + payloadSize > kMaxPendingBytes - bus.m_pendingBytes) {
        return luaL_error(state, "script message queue limit exceeded (1024 messages / 1 MiB)");
    }
    // All Lua validation precedes owned C++ objects (Lua errors use longjmp).
    MessagePayload payload;
    if (payloadType == LUA_TSTRING) {
        payload = std::string(payloadString, payloadSize);
    } else if (payloadType == LUA_TNUMBER && lua_isinteger(state, 4)) {
        payload = static_cast<std::int64_t>(lua_tointeger(state, 4));
    } else if (payloadType == LUA_TNUMBER) {
        payload = payloadNumber;
    } else if (payloadType == LUA_TBOOLEAN) {
        payload = lua_toboolean(state, 4) != 0;
    }
    bus.m_pending.push_back({targetIndex,
        static_cast<EntityId>(lua_tointeger(state, lua_upvalueindex(3))),
        std::string(name, nameSize), std::move(payload)});
    bus.m_pendingBytes += nameSize + payloadSize;
    lua_pushboolean(state, true);
    return 1;
}
} // namespace

void registerMessageBindings(lua_State* state, MessageBus& bus, Scene& scene, EntityId sender) {
    const luaL_Reg bindings[] = {
        // @lua fun sat.sendMessage(entity: EntityId, path: string, message: string, payload: nil|boolean|number|string): boolean
        {"sendMessage", guarded<sendMessage>},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &bus);
    lua_pushlightuserdata(state, &scene);
    lua_pushinteger(state, static_cast<lua_Integer>(sender));
    luaL_setfuncs(state, bindings, 3);
}

void pushMessagePayload(lua_State* state, const MessagePayload& payload) {
    switch (payload.index()) {
    case 1:
        lua_pushboolean(state, std::get<bool>(payload));
        break;
    case 2:
        lua_pushinteger(state, static_cast<lua_Integer>(std::get<std::int64_t>(payload)));
        break;
    case 3:
        lua_pushnumber(state, std::get<double>(payload));
        break;
    case 4: {
        const auto& value = std::get<std::string>(payload);
        lua_pushlstring(state, value.data(), value.size());
        break;
    }
    default:
        lua_pushnil(state);
        break;
    }
}
} // namespace sat::lua
