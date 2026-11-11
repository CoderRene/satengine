#pragma once

struct lua_State;

namespace sat::lua {

// Window-thread only. Shared by every component VM in one Play session.
// Frame values are snapshots; scale changes apply on the next update.
struct TimeState {
    float m_deltaSeconds = 0.0f;
    float m_unscaledDeltaSeconds = 0.0f;
    double m_elapsedSeconds = 0.0;
    double m_unscaledElapsedSeconds = 0.0;
    float m_timeScale = 1.0f;
};

// The borrowed state must outlive the VM. Registers sat.time on the top table.
void registerTimeBindings(lua_State* state, TimeState& time);

} // namespace sat::lua
