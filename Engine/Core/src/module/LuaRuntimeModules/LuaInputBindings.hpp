#pragma once

struct lua_State;

namespace sat {

class Input;

namespace lua {

// Registers sat.input, sat.Key, and sat.MouseButton onto the table at the top of the stack.
// Borrowed input may be null (idle state). Captured as an upvalue on input functions.
void registerInputBindings(lua_State* state, const Input* input);

} // namespace lua
} // namespace sat
