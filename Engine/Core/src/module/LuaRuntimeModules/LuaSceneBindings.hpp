#pragma once

struct lua_State;

namespace sat {

class Scene;
class PhysicsSystem;

namespace lua {

// Registers entity/transform/camera functions onto the table at the top of the stack.
// Scene* is captured as an upvalue on each function.
void registerSceneBindings(lua_State* state, Scene& scene, PhysicsSystem* physics = nullptr);

} // namespace lua
} // namespace sat
