#pragma once

#include "Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct lua_State;

namespace sat {
class LuaRuntime;

namespace lua {
using MessagePayload = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

struct ScriptTarget {
    EntityId m_entity;
    std::string m_path;
    // Borrowed from the session roster. Null until startup completes.
    LuaRuntime* m_instance = nullptr;
    bool m_accepting = true;
};

struct ScriptMessage {
    std::size_t m_targetIndex;
    EntityId m_sender;
    std::string m_name;
    MessagePayload m_payload;
};

// Window-thread only. Owned by the parent runtime; must outlive all component VMs.
// Two reusable buffers isolate delivery from messages enqueued by callbacks.
struct MessageBus {
    std::vector<ScriptTarget> m_targets;
    std::vector<ScriptMessage> m_pending;
    std::vector<ScriptMessage> m_delivery;
    std::size_t m_pendingBytes = 0;
    bool m_accepting = false;
};

// Registers sat.sendMessage on the top table. Captures borrowed bus and scene.
void registerMessageBindings(lua_State* state, MessageBus& bus, Scene& scene, EntityId sender);
void pushMessagePayload(lua_State* state, const MessagePayload& payload);

} // namespace lua
} // namespace sat
