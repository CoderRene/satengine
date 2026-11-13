#include "LuaAudioBindings.hpp"

#include "AudioSystem.hpp"
#include "LuaBindingUtils.hpp"

#include <cstddef>
#include <string_view>

namespace sat::lua {
namespace {

AudioSystem& checkedAudio(lua_State* state) {
    auto* audio = static_cast<AudioSystem*>(lua_touserdata(state, lua_upvalueindex(2)));
    if (!audio || !audio->running()) {
        luaL_error(state, "audio is not running");
    }
    return *audio;
}

int result(lua_State* state, const AudioSystem& audio, bool success) {
    return success ? 0 : luaL_error(state, "%s", audio.error().c_str());
}

template<bool (AudioSystem::*Operation)(EntityId)>
int entityOperation(lua_State* state) {
    auto& audio = checkedAudio(state);
    return result(state, audio, (audio.*Operation)(checkedEntity(state).id()));
}

template<bool (AudioSystem::*Operation)(EntityId, float)>
int entityParameter(lua_State* state) {
    auto& audio = checkedAudio(state);
    const auto id = checkedEntity(state).id();
    return result(state, audio, (audio.*Operation)(id, checkedFloat(state, 2)));
}

int isPlaying(lua_State* state) {
    const auto& audio = checkedAudio(state);
    lua_pushboolean(state, audio.isPlaying(checkedEntity(state).id()));
    return 1;
}

std::string_view checkedString(lua_State* state, int index) {
    std::size_t size = 0;
    const char* text = luaL_checklstring(state, index, &size);
    return {text, size};
}

int playOneShot(lua_State* state) {
    auto& audio = checkedAudio(state);
    const auto path = checkedString(state, 1);
    const DVec3 position{checkedDouble(state, 2), checkedDouble(state, 3), checkedDouble(state, 4)};
    return result(state, audio, audio.playOneShot(path, position, checkedFloat(state, 5)));
}

int setBusVolume(lua_State* state) {
    auto& audio = checkedAudio(state);
    const auto name = checkedString(state, 1);
    AudioBus bus;
    if (name == "sfx") {
        bus = AudioBus::Sfx;
    } else if (name == "music") {
        bus = AudioBus::Music;
    } else if (name == "ui") {
        bus = AudioBus::Ui;
    } else {
        return luaL_argerror(state, 1, "expected sfx, music or ui");
    }
    return result(state, audio, audio.setBusVolume(bus, checkedFloat(state, 2)));
}

int setMasterVolume(lua_State* state) {
    auto& audio = checkedAudio(state);
    audio.setMasterVolume(checkedFloat(state, 1));
    return result(state, audio, audio.error().empty());
}

} // namespace

void registerAudioBindings(lua_State* state, Scene& scene, AudioSystem* audio) {
    lua_newtable(state);
    const luaL_Reg bindings[] = {
        // @lua fun sat.audio.play(entity: EntityId)
        {"play", guarded<entityOperation<&AudioSystem::play>>},
        // @lua fun sat.audio.stop(entity: EntityId)
        {"stop", guarded<entityOperation<&AudioSystem::stopSound>>},
        // @lua fun sat.audio.pause(entity: EntityId)
        {"pause", guarded<entityOperation<&AudioSystem::pause>>},
        // @lua fun sat.audio.resume(entity: EntityId)
        {"resume", guarded<entityOperation<&AudioSystem::resume>>},
        // @lua fun sat.audio.isPlaying(entity: EntityId): boolean
        {"isPlaying", guarded<isPlaying>},
        // @lua fun sat.audio.setVolume(entity: EntityId, volume: number)
        {"setVolume", guarded<entityParameter<&AudioSystem::setVolume>>},
        // @lua fun sat.audio.setPitch(entity: EntityId, pitch: number)
        {"setPitch", guarded<entityParameter<&AudioSystem::setPitch>>},
        // @lua fun sat.audio.playOneShot(path: string, x: number, y: number, z: number, volume: number)
        {"playOneShot", guarded<playOneShot>},
        // @lua fun sat.audio.setBusVolume(bus: "sfx"|"music"|"ui", volume: number)
        {"setBusVolume", guarded<setBusVolume>},
        // @lua fun sat.audio.setMasterVolume(volume: number)
        {"setMasterVolume", guarded<setMasterVolume>},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &scene);
    lua_pushlightuserdata(state, audio);
    luaL_setfuncs(state, bindings, 2);
    lua_setfield(state, -2, "audio");
}

} // namespace sat::lua
