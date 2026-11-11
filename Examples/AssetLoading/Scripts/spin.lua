function onUpdate(deltaSeconds)
    local pitch, yaw, roll = sat.getRotation(self.entity)
    sat.setRotation(self.entity, pitch, (yaw + 30 * deltaSeconds) % 360, roll)
end
