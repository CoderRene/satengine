local audio = sat.audio
local phase = 0
local paused = false
local muted = false
local slow = false

function onStart()
    sat.log("Audio Demo | Space pauses the loop | Enter plays a chime | M mutes music | T toggles slow motion")
end

function onUpdate(deltaSeconds)
    local key = sat.Key
    if sat.input.keyPressed(key.Space) then
        if paused then
            audio.resume(self.entity)
        else
            audio.pause(self.entity)
        end
        paused = not paused
    end

    if sat.input.keyPressed(key.T) then
        slow = not slow
        sat.time.setTimeScale(slow and 0.5 or 1)
    end

    if sat.input.keyPressed(key.M) then
        muted = not muted
        audio.setBusVolume("music", muted and 0 or 1)
    end

    phase = (phase + deltaSeconds) % 6
    local x = 3 * math.sin(phase * math.pi / 3)
    sat.setPosition(self.entity, x, 0, 0)

    if sat.input.keyPressed(key.Enter) then
        audio.playOneShot("Assets/Audio/Chime.wav", x, 0, 0, 1)
    end
end
