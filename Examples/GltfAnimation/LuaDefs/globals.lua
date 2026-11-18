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

--- Optional. Receives queued messages before onUpdate at the next update boundary.
--- Sender may have been destroyed; check sat.isEntityValid before using it.
---@param sender EntityId
---@param message string
---@param payload nil|boolean|number|string
function onMessage(sender, message, payload) end

--- Optional. Called before each fixed physics tick; deltaSeconds is simulated time.
---@param deltaSeconds number
function onFixedUpdate(deltaSeconds) end

--- Optional. One event per entity pair, aggregated across compound shapes.
---@param other EntityId
function onCollisionEnter(other) end
---@param other EntityId
function onCollisionStay(other) end
--- The counterpart may have been destroyed; check sat.isEntityValid.
---@param other EntityId
function onCollisionExit(other) end
---@param other EntityId
function onTriggerEnter(other) end
---@param other EntityId
function onTriggerStay(other) end
---@param other EntityId
function onTriggerExit(other) end
