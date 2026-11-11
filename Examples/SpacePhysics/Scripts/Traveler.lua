local previousFrame
function onFixedUpdate(dt)
    local frame = sat.physics.frameId(self.entity)
    if frame ~= previousFrame then
        sat.log("Traveler transferred to frame:", frame)
        previousFrame = frame
    end
end
