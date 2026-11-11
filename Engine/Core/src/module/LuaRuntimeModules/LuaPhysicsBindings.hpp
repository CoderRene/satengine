#pragma once
struct lua_State;
namespace sat {
class Scene;
class PhysicsSystem;
namespace lua {
void registerPhysicsBindings(lua_State *state, Scene &scene,
                             PhysicsSystem *physics);
}
} // namespace sat
