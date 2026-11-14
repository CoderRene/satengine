-- Both cubes use this behavior. Tune each attachment in the Inspector.
sat.expose {
    spinning = true,
    target = sat.entityField(),
    speed2 = 45.0,
    test = "test",
}

---@class SpinSelf: ScriptSelf
---@field speed2 number
---@field spinning boolean
local cube = self --[[@as SpinSelf]]

function onUpdate(deltaSeconds)
    if cube.spinning then
        local x, y, z = sat.getRotation(cube.entity)
        sat.setRotation(cube.entity, x, (y + cube.speed2 * deltaSeconds) % 360, z)
    end
end
