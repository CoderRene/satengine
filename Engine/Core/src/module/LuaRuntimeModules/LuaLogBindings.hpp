#pragma once

struct lua_State;

namespace sat {

class ScriptLog;

namespace lua {

// Registers sat.log / sat.warn onto the table at the top of the stack.
// Borrowed log may be null (streams only). Captured as an upvalue.
void registerLogBindings(lua_State* state, ScriptLog* log);

} // namespace lua
} // namespace sat