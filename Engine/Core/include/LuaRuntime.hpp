#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace sat {
class Scene;
class Input;
class ScriptLog;
class PhysicsSystem;
class GameUi;
class AudioSystem;

// Script names are relative to Scripts/, using '/' separators (e.g. main.lua).
struct LuaScriptSource {
    std::string_view name;
    std::string_view source;
    // All entries in a bundle must use the same format. Binary chunks must come
    // from a trusted compiler matching this runtime Lua build.
    bool bytecode = false;
};

// Window-thread only. The borrowed scene must outlive this runtime.
// Scripts are trusted project code and have access to Lua's standard libraries.
// Entity IDs are opaque positive integers; retain them instead of scene indices.
class LuaRuntime {
public:
    explicit LuaRuntime(Scene& scene, const Input* input = nullptr, ScriptLog* log = nullptr,
                        PhysicsSystem* physics = nullptr, AudioSystem* audio = nullptr);
    // Borrowed input, physics and audio must outlive the runtime. Null input is idle.
    // The session starts and stops the borrowed physics and audio systems.
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&&) = delete;
    LuaRuntime& operator=(LuaRuntime&&) = delete;

    // Set while stopped. The borrowed UI must remain alive until stop().
    void setGameUi(GameUi* ui);

    // Starts enabled script components attached to scene entities. Each component
    // has a separate VM and self.entity (the stable owning entity ID).
    // No file is executed merely because it exists in Scripts/.
    // Failure disables updates. Call stop() to release the VM; restarting clears errors.
    [[nodiscard]] bool start(const std::filesystem::path& projectRoot);
    // Runs an embedded project without filesystem module lookup. Sources are
    // copied into the VM during this call and need not outlive it.
    [[nodiscard]] bool start(std::span<const LuaScriptSource> scripts);
    // Destroyed owners stop at callback boundaries, before any further updates.
    // Their onStop runs once with an invalid self.entity; use isEntityValid in Lua.
    // Queued onMessage callbacks run before onUpdate; messages sent during
    // delivery or updates wait until the following update.
    // Input delta is unscaled seconds. sat.time advances once per update;
    // onUpdate receives scaled seconds. Play resets elapsed time and scale.
    [[nodiscard]] bool update(float deltaSeconds);
    // Calls optional onStop() once after successful startup, then releases the VM.
    // Preserves the first failure. Repeated calls are harmless.
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] const std::string& error() const;

private:
    [[nodiscard]] bool startSource(std::string_view source, std::string_view chunkName,
                                   std::string_view modulePath,
                                   std::span<const LuaScriptSource> scripts, bool embedded, std::uint64_t ownerId);
    [[nodiscard]] bool startScripts(const std::filesystem::path& root,
                                    std::span<const LuaScriptSource> scripts, bool embedded);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace sat
