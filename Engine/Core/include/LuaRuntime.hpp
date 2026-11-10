#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace sat {
class Scene;
class Input;

// Script names are relative to Scripts/, using '/' separators (e.g. main.lua).
struct LuaScriptSource {
    std::string_view name;
    std::string_view source;
};

// Window-thread only. The borrowed scene must outlive this runtime.
// Scripts are trusted project code and have access to Lua's standard libraries.
// Entity IDs are one-based scene indices, valid until a host removes/reorders entities.
class LuaRuntime {
public:
    explicit LuaRuntime(Scene& scene, const Input* input = nullptr);
    // Borrowed input must outlive the runtime; null supplies an idle input state.
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&&) = delete;
    LuaRuntime& operator=(LuaRuntime&&) = delete;

    // Starts enabled script components attached to scene entities. Each component
    // has a separate VM and self.entity (a one-based owning entity ID).
    // No file is executed merely because it exists in Scripts/.
    // Failure disables updates. Call stop() to release the VM; restarting clears errors.
    [[nodiscard]] bool start(const std::filesystem::path& projectRoot);
    // Runs an embedded project without filesystem module lookup. Sources are
    // copied into the VM during this call and need not outlive it.
    [[nodiscard]] bool start(std::span<const LuaScriptSource> scripts);
    [[nodiscard]] bool update(float deltaSeconds);
    // Calls optional onStop() once after successful startup, then releases the VM.
    // Preserves the first failure. Repeated calls are harmless.
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] const std::string& error() const;

private:
    [[nodiscard]] bool startSource(std::string_view source, std::string_view chunkName,
                                   std::string_view modulePath,
                                   std::span<const LuaScriptSource> scripts, bool embedded, std::size_t ownerId);
    [[nodiscard]] bool startScripts(const std::filesystem::path& root,
                                    std::span<const LuaScriptSource> scripts, bool embedded);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace sat
