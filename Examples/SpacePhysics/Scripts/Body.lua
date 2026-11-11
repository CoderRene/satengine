local previousFrame
local elapsed = 0

function onStart()
    sat.physics.addImpulse(self.entity, 2, 0, 0)
end

function onFixedUpdate(dt)
    elapsed = elapsed + dt
    local frame = sat.physics.frameId(self.entity)
    if frame ~= previousFrame then
        sat.log("Physics frame:", frame)
        previousFrame = frame
    end
    if elapsed >= 2 then
        elapsed = 0
        local x, y, z = sat.getPosition(self.entity)
        local hit = sat.physics.raycast(x, y, z, 0, -1, 0, 50, self.entity)
        if hit then sat.log("Ground distance:", hit.distance) end
    end
end

function onCollisionEnter(other)
    sat.log("Collision with:", other)
end

function onTriggerEnter(other)
    sat.log("Entered trigger:", other)
end
