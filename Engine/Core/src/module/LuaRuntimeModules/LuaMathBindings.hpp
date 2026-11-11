#pragma once

struct lua_State;

namespace sat::lua {

// Registers stateless sat.math helpers on the table at the top of the stack.
// Vectors use numeric triples; rotations use engine Euler degrees.
void registerMathBindings(lua_State* state);

} // namespace sat::lua
