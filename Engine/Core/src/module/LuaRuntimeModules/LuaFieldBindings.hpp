#pragma once

#include "ScriptFields.hpp"

struct lua_State;

namespace sat::lua {

struct FieldBindings {
    ScriptFields m_defaults;
    // Borrowed only during the script chunk's initial execution.
    const ScriptFields* m_startupOverrides = nullptr;
    int m_selfRef = 0;
    bool m_discovery = false;
    bool m_declared = false;
    bool m_accepting = true;
};

// Registers sat.expose on the top table. The binding must outlive the VM.
void registerFieldBindings(lua_State* state, FieldBindings& fields);
void applyFieldEdits(lua_State* state, const FieldBindings& fields, const ScriptFields& before,
                     const ScriptFields& after);

} // namespace sat::lua
