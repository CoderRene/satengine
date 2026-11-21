local frame = 0
local time = 0
function onUpdate(deltaSeconds)
    frame = frame + 1
    time = time + deltaSeconds
    sat.setPosition(self.entity, math.sin(time) * 2.5, 2, 13)
    if frame == 30 then
        local mesh = sat.createEntity("Lua-only beacon")
        sat.addMesh(mesh)
        sat.setPosition(mesh, 1, 1, 8)
        sat.setMeshAsset(mesh, "Assets/" .. "LuaOnly.obj")
    end
end
