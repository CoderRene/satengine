#pragma once

struct lua_State;

namespace sat {
class AudioSystem;
class Scene;

namespace lua {
void registerAudioBindings(lua_State* state, Scene& scene, AudioSystem* audio);
}
} // namespace sat
