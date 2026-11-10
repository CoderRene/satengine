#include "LuaRuntime.hpp"

#include "module/LuaRuntimeModules/LuaInputBindings.hpp"
#include "module/LuaRuntimeModules/LuaPackageConfig.hpp"
#include "module/LuaRuntimeModules/LuaSceneBindings.hpp"
#include "module/LuaRuntimeModules/LuaLogBindings.hpp"

#include "ScriptLog.hpp"
#include "Scene.hpp"
#include "Input.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace sat {
namespace {
constexpr int kInstructionBudget = 1000000;

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
    explicit Impl(Scene& scene, const Input* input, ScriptLog* log)
    : m_scene(scene), m_input(input), m_log(log) {}
    ~Impl() { close(); }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    Scene& m_scene;
    const Input* m_input;
    ScriptLog* m_log = nullptr;
    lua_State* m_state = nullptr;
    bool m_running = false;
    bool m_started = false;
    EntityId m_ownerId = kInvalidEntityId;
    std::uint64_t m_lastDestructionRevision = 0;
    std::string m_error;
    std::string m_context;
    std::vector<std::unique_ptr<LuaRuntime>> m_instances;
    std::string m_scriptPath;

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

LuaRuntime::LuaRuntime(Scene& scene, const Input* input, ScriptLog* log)
    : m_impl(std::make_unique<Impl>(scene, input, log)) {}
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
        if (scripts[index].bytecode != scripts.front().bytecode) {
            return impl.fail("Cannot mix source and bytecode script bundles");
        }
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
        auto instance = std::make_unique<LuaRuntime>(impl.m_scene, impl.m_input, impl.m_log);
        instance->m_impl->m_context = "Entity " + attachment.entityName + " [Scripts/" + attachment.path + "]";
        instance->m_impl->m_scriptPath = attachment.path;
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
    lua::configurePackagePath(state, modulePath);
    if (embedded) {
        lua::configureEmbeddedSearchers(state, scripts);
    }

    lua_newtable(state);
    lua::registerSceneBindings(state, impl.m_scene);
    lua::registerInputBindings(state, impl.m_input);
    lua::registerLogBindings(state, impl.m_log);
    lua_setglobal(state, "sat");

    lua_newtable(state);
    lua_pushinteger(state, static_cast<lua_Integer>(ownerId));
    lua_setfield(state, -2, "entity");
    lua_setglobal(state, "self");

    const std::string chunkLabel(chunkName);
    if (luaL_loadbufferx(state, source.data(), source.size(), chunkLabel.c_str(), embedded && !scripts.empty() && scripts.front().bytecode ? "b" : "t") != LUA_OK) {
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
        if (!instance->running()) {
            continue;
        }
        const Entity* owner = impl.m_scene.entityById(instance->m_impl->m_ownerId);
        if (owner == nullptr) {
            continue; // retireDestroyed handles stop
        }
        const auto& path = instance->m_impl->m_scriptPath;
        const auto script = std::find_if(owner->scripts.begin(), owner->scripts.end(),
            [&](const ScriptComponent& component) { return component.path == path; });
        if (script == owner->scripts.end() || !script->enabled) {
            continue;
        }
        if (!instance->update(deltaSeconds)) {
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
