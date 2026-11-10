-- Editor-only EmmyLua stubs for per-script globals injected by LuaRuntime.
-- Not loaded by LuaRuntime; keep in sync with LuaRuntime::startSource.

---@meta

---@class ScriptSelf
--- Stable owning entity id. Invalid after destruction; use sat.isEntityValid.
---@field entity EntityId
self = {}

--- Optional. Called once after the chunk loads successfully.
function onStart() end

--- Optional. Called each frame while the script component is enabled.
---@param deltaSeconds number
function onUpdate(deltaSeconds) end

--- Optional. Called once when the runtime stops or the owner is destroyed.
--- When the owner is already gone, self.entity is invalid; use sat.isEntityValid.
function onStop() end
