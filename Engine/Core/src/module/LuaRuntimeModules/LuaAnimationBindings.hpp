#pragma once
struct lua_State;
namespace sat {
class Scene;
class AnimationSystem;
namespace lua { void registerAnimationBindings(lua_State* state, Scene& scene, AnimationSystem* animation); }
}
