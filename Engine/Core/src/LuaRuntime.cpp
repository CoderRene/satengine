#include "LuaRuntime.hpp"

#include "Scene.hpp"
#include "Input.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace sat {
namespace {
constexpr int kInstructionBudget = 1000000;

Scene& sceneFor(lua_State* state) {
    return *static_cast<Scene*>(lua_touserdata(state, lua_upvalueindex(1)));
}

Entity& checkedEntity(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    Scene& scene = sceneFor(state);
    Entity* entity = id > 0 ? scene.entityById(static_cast<EntityId>(id)) : nullptr;
    if (entity == nullptr) {
        luaL_error(state, "invalid or destroyed entity ID");
    }
    return *entity;
}

float checkedFloat(lua_State* state, int argument) {
    const lua_Number value = luaL_checknumber(state, argument);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        luaL_argerror(state, argument, "expected a finite float");
    }
    return static_cast<float>(value);
}

int createEntity(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const EntityId id = sceneFor(state).createEntity(name).id();
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

int createCube(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    Entity& entity = sceneFor(state).createEntity(name);
    entity.hasMesh = true;
    lua_pushinteger(state, static_cast<lua_Integer>(entity.id()));
    return 1;
}

int createCamera(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    Entity& entity = sceneFor(state).createEntity(name);
    entity.hasCamera = true;
    lua_pushinteger(state, static_cast<lua_Integer>(entity.id()));
    return 1;
}

int findEntity(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const auto& entities = sceneFor(state).entities();
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (entities[index].name == name) {
            lua_pushinteger(state, static_cast<lua_Integer>(entities[index].id()));
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

int destroyEntity(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    lua_pushboolean(state, id > 0 && sceneFor(state).destroyEntity(static_cast<EntityId>(id)));
    return 1;
}

int isEntityValid(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    lua_pushboolean(state, id > 0 && sceneFor(state).entityById(static_cast<EntityId>(id)) != nullptr);
    return 1;
}

int getName(lua_State* state) {
    const auto& name = checkedEntity(state).name;
    lua_pushlstring(state, name.data(), name.size());
    return 1;
}

int getEntityCount(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(sceneFor(state).entityCount()));
    return 1;
}

int getEntities(lua_State* state) {
    const auto& entities = sceneFor(state).entities();
    const auto capacity = std::min(entities.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    lua_createtable(state, static_cast<int>(capacity), 0);
    lua_Integer index = 1;
    for (const auto& entity : entities) {
        lua_pushinteger(state, static_cast<lua_Integer>(entity.id()));
        lua_rawseti(state, -2, index++);
    }
    return 1;
}

template<Vec3 Transform::* member>
int getVector(lua_State* state) {
    const Vec3 value = checkedEntity(state).transform.*member;
    lua_pushnumber(state, value.x);
    lua_pushnumber(state, value.y);
    lua_pushnumber(state, value.z);
    return 3;
}

template<Vec3 Transform::* member>
int setVector(lua_State* state) {
    Entity& entity = checkedEntity(state);
    const float x = checkedFloat(state, 2);
    const float y = checkedFloat(state, 3);
    const float z = checkedFloat(state, 4);
    entity.transform.*member = {x, y, z};
    return 0;
}

int setMainCamera(lua_State* state) {
    const EntityId id = checkedEntity(state).id();
    if (!sceneFor(state).setMainCameraById(id)) {
        return luaL_error(state, "entity has no camera");
    }
    return 0;
}

// Never allow C++ exceptions across the Lua C boundary. Lua errors are raised
// only after exception handlers (and their C++ objects) have been unwound.
template<int (*function)(lua_State*)>
int guarded(lua_State* state) {
    try {
        return function(state);
    } catch (...) {
    }
    return luaL_error(state, "engine binding failed (C++ exception)");
}

// All lookup strings live on the Lua stack; errors may longjmp without skipping
// C++ destructors. require() receives a loader function and its virtual filename.
int embeddedSearcher(lua_State* state) {
    const char* module = luaL_checkstring(state, 1);
    const char* relative = luaL_gsub(state, module, ".", "/");
    lua_pushfstring(state, "%s.lua", relative);
    lua_pushvalue(state, -1);
    lua_gettable(state, lua_upvalueindex(1));
    if (lua_isnil(state, -1)) {
        lua_pop(state, 2);
        lua_pushfstring(state, "%s/init.lua", relative);
        lua_pushvalue(state, -1);
        lua_gettable(state, lua_upvalueindex(1));
    }
    if (lua_isnil(state, -1)) {
        lua_pushfstring(state, "\n\tno embedded Lua module '%s'", module);
        return 1;
    }
    std::size_t size = 0;
    const char* source = lua_tolstring(state, -1, &size);
    lua_pushfstring(state, "@Scripts/%s", lua_tostring(state, -2));
    if (luaL_loadbufferx(state, source, size, lua_tostring(state, -1), "t") != LUA_OK) {
        return lua_error(state);
    }
    lua_pushvalue(state, -2);
    return 2;
}

const Input& inputFor(lua_State* state) {
    static const Input kIdleInput;
    const auto* input = static_cast<const Input*>(lua_touserdata(state, lua_upvalueindex(1)));
    return input != nullptr ? *input : kIdleInput;
}
int keyPressed(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 348) { return luaL_argerror(state, 1, "invalid input code"); }
    lua_pushboolean(state, inputFor(state).keyPressed(static_cast<Key>(value)));
    return 1;
}
int keyHeld(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 348) { return luaL_argerror(state, 1, "invalid input code"); }
    lua_pushboolean(state, inputFor(state).keyHeld(static_cast<Key>(value)));
    return 1;
}
int keyReleased(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 348) { return luaL_argerror(state, 1, "invalid input code"); }
    lua_pushboolean(state, inputFor(state).keyReleased(static_cast<Key>(value)));
    return 1;
}
int mousePressed(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 7) { return luaL_argerror(state, 1, "invalid input code"); }
    lua_pushboolean(state, inputFor(state).mousePressed(static_cast<MouseButton>(value)));
    return 1;
}
int mouseHeld(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 7) { return luaL_argerror(state, 1, "invalid input code"); }
    lua_pushboolean(state, inputFor(state).mouseHeld(static_cast<MouseButton>(value)));
    return 1;
}
int mouseReleased(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 7) { return luaL_argerror(state, 1, "invalid input code"); }
    lua_pushboolean(state, inputFor(state).mouseReleased(static_cast<MouseButton>(value)));
    return 1;
}
int cursorPosition(lua_State* state) {
    const auto position = inputFor(state).cursorPosition();
    lua_pushnumber(state, position.x);
    lua_pushnumber(state, position.y);
    return 2;
}

int traceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    luaL_traceback(state, state, message != nullptr ? message : "non-string Lua error", 1);
    return 1;
}

void instructionLimit(lua_State* state, lua_Debug*) {
    luaL_error(state, "script exceeded instruction budget");
}
} // namespace

struct LuaRuntime::Impl {
    explicit Impl(Scene& scene, const Input* input) : m_scene(scene), m_input(input) {}
    ~Impl() { close(); }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    Scene& m_scene;
    const Input* m_input;
    lua_State* m_state = nullptr;
    bool m_running = false;
    bool m_started = false;
    EntityId m_ownerId = kInvalidEntityId;
    std::uint64_t m_lastDestructionRevision = 0;
    std::string m_error;
    std::string m_context;
    std::vector<std::unique_ptr<LuaRuntime>> m_instances;

    void close() {
        m_instances.clear();
        if (m_state != nullptr) {
            lua_close(m_state);
            m_state = nullptr;
        }
        m_running = false;
        m_started = false;
    }

    bool retireDestroyed(bool force = false) {
        if (!force && m_lastDestructionRevision == m_scene.destructionRevision()) {
            return true;
        }
        // A stop callback may destroy an owner already visited in this sweep.
        // Only rescan when destruction actually changes; normal updates are O(n).
        do {
            m_lastDestructionRevision = m_scene.destructionRevision();
            for (const auto& instance : m_instances) {
                auto& child = *instance->m_impl;
                if (child.m_state != nullptr && m_scene.entityById(child.m_ownerId) == nullptr) {
                    instance->stop();
                    if (!instance->error().empty()) {
                        return fail(instance->error());
                    }
                }
            }
        } while (m_lastDestructionRevision != m_scene.destructionRevision());
        return true;
    }

    bool fail(const std::string& message) {
        if (m_error.empty()) {
            m_error = m_context.empty() ? message : m_context + ": " + message;
        }
        m_running = false;
        return false;
    }

    bool call(int argumentCount) {
        const int functionIndex = lua_gettop(m_state) - argumentCount;
        lua_pushcfunction(m_state, traceback);
        lua_insert(m_state, functionIndex);
        lua_sethook(m_state, instructionLimit, LUA_MASKCOUNT, kInstructionBudget);
        const int result = lua_pcall(m_state, argumentCount, 0, functionIndex);
        lua_sethook(m_state, nullptr, 0, 0);
        if (result != LUA_OK) {
            const char* message = lua_tostring(m_state, -1);
            fail(message != nullptr ? message : "Unknown Lua error");
            lua_settop(m_state, functionIndex - 1);
            return false;
        }
        lua_pop(m_state, 1); // traceback handler
        return true;
    }

    bool callback(const char* name, bool withDelta = false, float delta = 0.0f) {
        lua_getglobal(m_state, name);
        if (lua_isnil(m_state, -1)) {
            lua_pop(m_state, 1);
            return true;
        }
        if (!lua_isfunction(m_state, -1)) {
            lua_pop(m_state, 1);
            return fail(std::string(name) + " must be a function or nil");
        }
        if (withDelta) {
            lua_pushnumber(m_state, delta);
        }
        return call(withDelta ? 1 : 0);
    }
};

LuaRuntime::LuaRuntime(Scene& scene, const Input* input) : m_impl(std::make_unique<Impl>(scene, input)) {}
LuaRuntime::~LuaRuntime() = default;

bool LuaRuntime::start(const std::filesystem::path& projectRoot) {
    return startScripts(projectRoot, {}, false);
}

bool LuaRuntime::start(std::span<const LuaScriptSource> scripts) {
    return startScripts({}, scripts, true);
}

bool LuaRuntime::startScripts(const std::filesystem::path& root,
                              std::span<const LuaScriptSource> scripts, bool embedded) {
    stop();
    auto& impl = *m_impl;
    impl.m_error.clear();
    impl.m_context.clear();
    for (std::size_t index = 0; index < scripts.size(); ++index) {
        if (!isValidScriptPath(scripts[index].name)) {
            return impl.fail("Invalid embedded script name");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (scripts[previous].name == scripts[index].name) {
                return impl.fail("Duplicate embedded script: " + std::string(scripts[index].name));
            }
        }
    }
    // Snapshot the roster before executing user code: creating entities may grow
    // the scene vector. The roster remains fixed until the next Play.
    struct Attachment {
        EntityId ownerId;
        std::string entityName;
        std::string path;
    };
    std::vector<Attachment> attachments;
    for (std::size_t index = 0; index < impl.m_scene.entities().size(); ++index) {
        const auto& entity = impl.m_scene.entities()[index];
        for (const auto& component : entity.scripts) {
            if (component.enabled) {
                if (!isValidScriptPath(component.path)) {
                    return impl.fail("Invalid script path on entity " + entity.name);
                }
                attachments.push_back({entity.id(), entity.name, component.path});
            }
        }
    }
    std::string modulePath;
    if (!embedded && !attachments.empty()) {
        const auto directory = std::filesystem::absolute(root / "Scripts").generic_string();
        modulePath = directory + "/?.lua;" + directory + "/?/init.lua";
    }
    for (const auto& attachment : attachments) {
        if (impl.m_scene.entityById(attachment.ownerId) == nullptr) {
            continue; // Earlier startup code may destroy an unstarted owner.
        }
        std::string ownedSource;
        std::string_view source;
        if (embedded) {
            const auto found = std::find_if(scripts.begin(), scripts.end(), [&](const LuaScriptSource& script) {
                return script.name == attachment.path;
            });
            if (found == scripts.end()) {
                impl.fail("Entity " + attachment.entityName + ": missing embedded script Scripts/" + attachment.path);
                stop();
                return false;
            }
            source = found->source;
        } else {
            const auto filePath = root / "Scripts" / std::filesystem::path(std::u8string(attachment.path.begin(), attachment.path.end()));
            std::ifstream file(filePath, std::ios::binary);
            if (!file) {
                impl.fail("Entity " + attachment.entityName + ": cannot read script " + filePath.string());
                stop();
                return false;
            }
            ownedSource.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            if (file.bad()) {
                impl.fail("Failed reading script: " + filePath.string());
                stop();
                return false;
            }
            source = ownedSource;
        }
        auto instance = std::make_unique<LuaRuntime>(impl.m_scene, impl.m_input);
        instance->m_impl->m_context = "Entity " + attachment.entityName + " [Scripts/" + attachment.path + "]";
        if (!instance->startSource(source, "@Scripts/" + attachment.path, modulePath,
                                   scripts, embedded, attachment.ownerId)) {
            impl.fail(instance->error());
            stop();
            return false;
        }
        impl.m_instances.push_back(std::move(instance));
        if (!impl.retireDestroyed()) {
            stop();
            return false;
        }
    }
    if (!impl.retireDestroyed()) {
        stop();
        return false;
    }
    impl.m_running = true;
    return true;
}

bool LuaRuntime::startSource(std::string_view source, std::string_view chunkName,
                             std::string_view modulePath,
                             std::span<const LuaScriptSource> scripts, bool embedded, std::uint64_t ownerId) {
    auto& impl = *m_impl;
    impl.m_ownerId = ownerId;
    impl.m_state = luaL_newstate();
    if (impl.m_state == nullptr) {
        return impl.fail("Cannot allocate Lua state");
    }
    lua_State* state = impl.m_state;
    luaL_openlibs(state);
    // Assign package.path directly: project paths are data, never Lua code.
    lua_getglobal(state, "package");
    lua_pushlstring(state, modulePath.data(), modulePath.size());
    lua_setfield(state, -2, "path");
    if (embedded) {
        lua_pushliteral(state, "");
        lua_setfield(state, -2, "cpath");
        lua_getfield(state, -1, "searchers");
        lua_newtable(state);
        lua_rawgeti(state, -2, 1); // Retain Lua's preload searcher for built-ins.
        lua_rawseti(state, -2, 1);
        lua_newtable(state);
        for (const auto& script : scripts) {
            lua_pushlstring(state, script.name.data(), script.name.size());
            lua_pushlstring(state, script.source.data(), script.source.size());
            lua_rawset(state, -3);
        }
        lua_pushcclosure(state, embeddedSearcher, 1);
        lua_rawseti(state, -2, 2);
        lua_setfield(state, -3, "searchers");
        lua_pop(state, 1); // Original searchers.
    }
    lua_pop(state, 1); // package

    const luaL_Reg bindings[] = {
        {"createEntity", guarded<createEntity>},
        {"createCube", guarded<createCube>},
        {"createCamera", guarded<createCamera>},
        {"findEntity", guarded<findEntity>},
        {"destroyEntity", guarded<destroyEntity>},
        {"isEntityValid", guarded<isEntityValid>},
        {"getName", guarded<getName>},
        {"getEntities", guarded<getEntities>},
        {"getEntityCount", guarded<getEntityCount>},
        {"getPosition", guarded<getVector<&Transform::position>>},
        {"setPosition", guarded<setVector<&Transform::position>>},
        {"getRotation", guarded<getVector<&Transform::rotation>>},
        {"setRotation", guarded<setVector<&Transform::rotation>>},
        {"getScale", guarded<getVector<&Transform::scale>>},
        {"setScale", guarded<setVector<&Transform::scale>>},
        {"setMainCamera", guarded<setMainCamera>},
        {nullptr, nullptr}
    };
    lua_newtable(state);
    lua_pushlightuserdata(state, &impl.m_scene);
    luaL_setfuncs(state, bindings, 1);
    lua_newtable(state);
    const luaL_Reg inputBindings[] = {
        {"keyPressed", keyPressed},
        {"keyHeld", keyHeld},
        {"keyReleased", keyReleased},
        {"mousePressed", mousePressed},
        {"mouseHeld", mouseHeld},
        {"mouseReleased", mouseReleased},
        {"cursorPosition", cursorPosition},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, const_cast<Input*>(impl.m_input));
    luaL_setfuncs(state, inputBindings, 1);
    lua_setfield(state, -2, "input");
    lua_newtable(state);
    lua_pushinteger(state, 32); lua_setfield(state, -2, "Space");
    lua_pushinteger(state, 39); lua_setfield(state, -2, "Apostrophe");
    lua_pushinteger(state, 44); lua_setfield(state, -2, "Comma");
    lua_pushinteger(state, 45); lua_setfield(state, -2, "Minus");
    lua_pushinteger(state, 46); lua_setfield(state, -2, "Period");
    lua_pushinteger(state, 47); lua_setfield(state, -2, "Slash");
    lua_pushinteger(state, 48); lua_setfield(state, -2, "Digit0");
    lua_pushinteger(state, 49); lua_setfield(state, -2, "Digit1");
    lua_pushinteger(state, 50); lua_setfield(state, -2, "Digit2");
    lua_pushinteger(state, 51); lua_setfield(state, -2, "Digit3");
    lua_pushinteger(state, 52); lua_setfield(state, -2, "Digit4");
    lua_pushinteger(state, 53); lua_setfield(state, -2, "Digit5");
    lua_pushinteger(state, 54); lua_setfield(state, -2, "Digit6");
    lua_pushinteger(state, 55); lua_setfield(state, -2, "Digit7");
    lua_pushinteger(state, 56); lua_setfield(state, -2, "Digit8");
    lua_pushinteger(state, 57); lua_setfield(state, -2, "Digit9");
    lua_pushinteger(state, 59); lua_setfield(state, -2, "Semicolon");
    lua_pushinteger(state, 61); lua_setfield(state, -2, "Equal");
    lua_pushinteger(state, 65); lua_setfield(state, -2, "A");
    lua_pushinteger(state, 66); lua_setfield(state, -2, "B");
    lua_pushinteger(state, 67); lua_setfield(state, -2, "C");
    lua_pushinteger(state, 68); lua_setfield(state, -2, "D");
    lua_pushinteger(state, 69); lua_setfield(state, -2, "E");
    lua_pushinteger(state, 70); lua_setfield(state, -2, "F");
    lua_pushinteger(state, 71); lua_setfield(state, -2, "G");
    lua_pushinteger(state, 72); lua_setfield(state, -2, "H");
    lua_pushinteger(state, 73); lua_setfield(state, -2, "I");
    lua_pushinteger(state, 74); lua_setfield(state, -2, "J");
    lua_pushinteger(state, 75); lua_setfield(state, -2, "K");
    lua_pushinteger(state, 76); lua_setfield(state, -2, "L");
    lua_pushinteger(state, 77); lua_setfield(state, -2, "M");
    lua_pushinteger(state, 78); lua_setfield(state, -2, "N");
    lua_pushinteger(state, 79); lua_setfield(state, -2, "O");
    lua_pushinteger(state, 80); lua_setfield(state, -2, "P");
    lua_pushinteger(state, 81); lua_setfield(state, -2, "Q");
    lua_pushinteger(state, 82); lua_setfield(state, -2, "R");
    lua_pushinteger(state, 83); lua_setfield(state, -2, "S");
    lua_pushinteger(state, 84); lua_setfield(state, -2, "T");
    lua_pushinteger(state, 85); lua_setfield(state, -2, "U");
    lua_pushinteger(state, 86); lua_setfield(state, -2, "V");
    lua_pushinteger(state, 87); lua_setfield(state, -2, "W");
    lua_pushinteger(state, 88); lua_setfield(state, -2, "X");
    lua_pushinteger(state, 89); lua_setfield(state, -2, "Y");
    lua_pushinteger(state, 90); lua_setfield(state, -2, "Z");
    lua_pushinteger(state, 91); lua_setfield(state, -2, "LeftBracket");
    lua_pushinteger(state, 92); lua_setfield(state, -2, "Backslash");
    lua_pushinteger(state, 93); lua_setfield(state, -2, "RightBracket");
    lua_pushinteger(state, 96); lua_setfield(state, -2, "GraveAccent");
    lua_pushinteger(state, 161); lua_setfield(state, -2, "World1");
    lua_pushinteger(state, 162); lua_setfield(state, -2, "World2");
    lua_pushinteger(state, 256); lua_setfield(state, -2, "Escape");
    lua_pushinteger(state, 257); lua_setfield(state, -2, "Enter");
    lua_pushinteger(state, 258); lua_setfield(state, -2, "Tab");
    lua_pushinteger(state, 259); lua_setfield(state, -2, "Backspace");
    lua_pushinteger(state, 260); lua_setfield(state, -2, "Insert");
    lua_pushinteger(state, 261); lua_setfield(state, -2, "Delete");
    lua_pushinteger(state, 262); lua_setfield(state, -2, "Right");
    lua_pushinteger(state, 263); lua_setfield(state, -2, "Left");
    lua_pushinteger(state, 264); lua_setfield(state, -2, "Down");
    lua_pushinteger(state, 265); lua_setfield(state, -2, "Up");
    lua_pushinteger(state, 266); lua_setfield(state, -2, "PageUp");
    lua_pushinteger(state, 267); lua_setfield(state, -2, "PageDown");
    lua_pushinteger(state, 268); lua_setfield(state, -2, "Home");
    lua_pushinteger(state, 269); lua_setfield(state, -2, "End");
    lua_pushinteger(state, 280); lua_setfield(state, -2, "CapsLock");
    lua_pushinteger(state, 281); lua_setfield(state, -2, "ScrollLock");
    lua_pushinteger(state, 282); lua_setfield(state, -2, "NumLock");
    lua_pushinteger(state, 283); lua_setfield(state, -2, "PrintScreen");
    lua_pushinteger(state, 284); lua_setfield(state, -2, "Pause");
    lua_pushinteger(state, 290); lua_setfield(state, -2, "F1");
    lua_pushinteger(state, 291); lua_setfield(state, -2, "F2");
    lua_pushinteger(state, 292); lua_setfield(state, -2, "F3");
    lua_pushinteger(state, 293); lua_setfield(state, -2, "F4");
    lua_pushinteger(state, 294); lua_setfield(state, -2, "F5");
    lua_pushinteger(state, 295); lua_setfield(state, -2, "F6");
    lua_pushinteger(state, 296); lua_setfield(state, -2, "F7");
    lua_pushinteger(state, 297); lua_setfield(state, -2, "F8");
    lua_pushinteger(state, 298); lua_setfield(state, -2, "F9");
    lua_pushinteger(state, 299); lua_setfield(state, -2, "F10");
    lua_pushinteger(state, 300); lua_setfield(state, -2, "F11");
    lua_pushinteger(state, 301); lua_setfield(state, -2, "F12");
    lua_pushinteger(state, 302); lua_setfield(state, -2, "F13");
    lua_pushinteger(state, 303); lua_setfield(state, -2, "F14");
    lua_pushinteger(state, 304); lua_setfield(state, -2, "F15");
    lua_pushinteger(state, 305); lua_setfield(state, -2, "F16");
    lua_pushinteger(state, 306); lua_setfield(state, -2, "F17");
    lua_pushinteger(state, 307); lua_setfield(state, -2, "F18");
    lua_pushinteger(state, 308); lua_setfield(state, -2, "F19");
    lua_pushinteger(state, 309); lua_setfield(state, -2, "F20");
    lua_pushinteger(state, 310); lua_setfield(state, -2, "F21");
    lua_pushinteger(state, 311); lua_setfield(state, -2, "F22");
    lua_pushinteger(state, 312); lua_setfield(state, -2, "F23");
    lua_pushinteger(state, 313); lua_setfield(state, -2, "F24");
    lua_pushinteger(state, 314); lua_setfield(state, -2, "F25");
    lua_pushinteger(state, 320); lua_setfield(state, -2, "Keypad0");
    lua_pushinteger(state, 321); lua_setfield(state, -2, "Keypad1");
    lua_pushinteger(state, 322); lua_setfield(state, -2, "Keypad2");
    lua_pushinteger(state, 323); lua_setfield(state, -2, "Keypad3");
    lua_pushinteger(state, 324); lua_setfield(state, -2, "Keypad4");
    lua_pushinteger(state, 325); lua_setfield(state, -2, "Keypad5");
    lua_pushinteger(state, 326); lua_setfield(state, -2, "Keypad6");
    lua_pushinteger(state, 327); lua_setfield(state, -2, "Keypad7");
    lua_pushinteger(state, 328); lua_setfield(state, -2, "Keypad8");
    lua_pushinteger(state, 329); lua_setfield(state, -2, "Keypad9");
    lua_pushinteger(state, 330); lua_setfield(state, -2, "KeypadDecimal");
    lua_pushinteger(state, 331); lua_setfield(state, -2, "KeypadDivide");
    lua_pushinteger(state, 332); lua_setfield(state, -2, "KeypadMultiply");
    lua_pushinteger(state, 333); lua_setfield(state, -2, "KeypadSubtract");
    lua_pushinteger(state, 334); lua_setfield(state, -2, "KeypadAdd");
    lua_pushinteger(state, 335); lua_setfield(state, -2, "KeypadEnter");
    lua_pushinteger(state, 336); lua_setfield(state, -2, "KeypadEqual");
    lua_pushinteger(state, 340); lua_setfield(state, -2, "LeftShift");
    lua_pushinteger(state, 341); lua_setfield(state, -2, "LeftControl");
    lua_pushinteger(state, 342); lua_setfield(state, -2, "LeftAlt");
    lua_pushinteger(state, 343); lua_setfield(state, -2, "LeftSuper");
    lua_pushinteger(state, 344); lua_setfield(state, -2, "RightShift");
    lua_pushinteger(state, 345); lua_setfield(state, -2, "RightControl");
    lua_pushinteger(state, 346); lua_setfield(state, -2, "RightAlt");
    lua_pushinteger(state, 347); lua_setfield(state, -2, "RightSuper");
    lua_pushinteger(state, 348); lua_setfield(state, -2, "Menu");
    lua_setfield(state, -2, "Key");
    lua_newtable(state);
    lua_pushinteger(state, 0); lua_setfield(state, -2, "Left");
    lua_pushinteger(state, 1); lua_setfield(state, -2, "Right");
    lua_pushinteger(state, 2); lua_setfield(state, -2, "Middle");
    lua_pushinteger(state, 3); lua_setfield(state, -2, "Button4");
    lua_pushinteger(state, 4); lua_setfield(state, -2, "Button5");
    lua_pushinteger(state, 5); lua_setfield(state, -2, "Button6");
    lua_pushinteger(state, 6); lua_setfield(state, -2, "Button7");
    lua_pushinteger(state, 7); lua_setfield(state, -2, "Button8");
    lua_setfield(state, -2, "MouseButton");
    lua_setglobal(state, "sat");
    lua_newtable(state);
    lua_pushinteger(state, static_cast<lua_Integer>(ownerId));
    lua_setfield(state, -2, "entity");
    lua_setglobal(state, "self");

    const std::string chunkLabel(chunkName);
    if (luaL_loadbufferx(state, source.data(), source.size(), chunkLabel.c_str(), "t") != LUA_OK) {
        impl.fail(lua_tostring(state, -1));
        impl.close();
        return false;
    }
    if (!impl.call(0) || (impl.m_scene.entityById(impl.m_ownerId) != nullptr && !impl.callback("onStart"))) {
        impl.close();
        return false;
    }
    impl.m_started = true;
    impl.m_running = true;
    return true;
}

bool LuaRuntime::update(float deltaSeconds) {
    auto& impl = *m_impl;
    if (!impl.m_running) {
        return false;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
        return impl.fail("deltaSeconds must be finite and nonnegative");
    }
    if (impl.m_state != nullptr) {
        return impl.callback("onUpdate", true, deltaSeconds);
    }
    if (!impl.retireDestroyed(true)) {
        return false;
    }
    for (const auto& instance : impl.m_instances) {
        if (instance->running() && !instance->update(deltaSeconds)) {
            return impl.fail(instance->error());
        }
        if (!impl.retireDestroyed()) {
            return false;
        }
    }
    return true;
}

void LuaRuntime::stop() {
    auto& impl = *m_impl;
    for (const auto& instance : impl.m_instances) {
        instance->stop();
        if (!instance->error().empty()) {
            impl.fail(instance->error());
        }
    }
    if (impl.m_started) {
        impl.m_started = false;
        impl.callback("onStop");
    }
    impl.close();
}
bool LuaRuntime::running() const { return m_impl->m_running; }
const std::string& LuaRuntime::error() const { return m_impl->m_error; }
} // namespace sat
