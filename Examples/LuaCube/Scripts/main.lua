-- This behavior runs only because Scene.sat attaches it to Lua Cube.
function onUpdate(deltaSeconds)
    local x, y, z = sat.getRotation(self.entity)
    sat.setRotation(self.entity, x, (y + 45 * deltaSeconds) % 360, z)
end
