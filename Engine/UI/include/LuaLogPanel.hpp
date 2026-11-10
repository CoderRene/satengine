#pragma once

#include <memory>

namespace sat {

class ScriptLog;

class LuaLogPanel {
public:
    explicit LuaLogPanel(ScriptLog& log);
    ~LuaLogPanel();

    LuaLogPanel(const LuaLogPanel&) = delete;
    LuaLogPanel& operator=(const LuaLogPanel&) = delete;

    // When focus is true, the Lua Log window is focused this frame.
    void onImGuiRender(bool focus);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat