# Audio

SatEngine plays WAV, Ogg Vorbis, MP3, and FLAC from project-relative paths such as
`Assets/Audio/engine.ogg`. The editor reads the files from the project and GamePlayer
reads the same paths from the pak. The pak builder also groups AAC, Opus, and AIFF
files as audio. Playback supports the four formats listed above.

## Authoring a scene

In the Inspector, use **Add Component** to add an **Audio Source** or **Audio
Listener**. Select the source clip and bus in the Inspector. Open **Scene >
Audio Settings** to set gains, voice limits, and time scaling for each bus.
Scenes save as `SatScene 9`. Versions 1–8 still load. Versions 1–3 have no audio
components.

- The line after physics settings contains master gain, SFX gain, music gain,
  UI gain, speed of sound, the mixed voice limit, the virtual voice limit, and
  one time-scale flag per bus in SFX, Music, UI order. The defaults are
  `1 1 1 1 343 64 0 0 0 0`.
- At the end of each entity's physics data, a line contains the audio source and
  listener flags, each written as `0` or `1`.
- If the source flag is `1`, the next line contains the quoted clip path, gain,
  pitch, loop, play-on-start, spatial, minimum distance, maximum distance,
  rolloff, doppler factor, bus index, and signed priority. Bus indices are `0` for SFX, `1` for
  music, and `2` for UI.

For example, the following entity tail adds a looping spatial source that starts
when Play begins.

```text
1 0
"Assets/Audio/engine.ogg" 1 1 1 1 1 1 100 1 1 0 0
```

An empty clip path leaves a source unassigned and skips autoplay. An assigned
autoplay clip that fails to load or exceeds the available voice count reports an
error and stops Play startup. Stop releases all voices and resets runtime audio
controls on the next Play session.

Audio uses the engine's left-handed world with +Y up and +Z forward. Positions
and attenuation distances are in meters. Pitch is a positive multiplier, and
volume is a nonnegative linear gain where `0` is silent and `1` is unity.
Speed of sound is in meters per second. Spatial audio uses the first explicit
listener in scene order, then the main camera. Spatial voices are silent while
neither is available. Nonspatial voices play independently of the listener.
The listener's translation is subtracted in double precision. Doppler uses each body's world velocity from physics, scaled by the simulation
time scale. Transforms without a physics body use position changes over unscaled
frame time.

## Lua

Call these functions from a running Play session. Entity arguments use stable IDs
such as `self.entity`. Mutating calls raise Lua errors on failure. Every audio
function raises `audio is not running` when the session has no active audio system.

| Function | Behavior |
| --- | --- |
| `sat.audio.play(entity)` | Play the entity's assigned source from the beginning. |
| `sat.audio.stop(entity)` | Release the entity's voice. |
| `sat.audio.pause(entity)` | Pause and retain the current voice and cursor. |
| `sat.audio.resume(entity)` | Resume a retained voice. |
| `sat.audio.isPlaying(entity)` | Return whether the entity has a playing voice. |
| `sat.audio.setVolume(entity, volume)` | Set the active voice's linear gain. |
| `sat.audio.setPitch(entity, pitch)` | Set the active voice's pitch multiplier. |
| `sat.audio.playOneShot(path, x, y, z, volume)` | Play a spatial SFX voice at a fixed world position. |
| `sat.audio.setBusVolume(bus, volume)` | Set gain on `"sfx"`, `"music"`, or `"ui"`. |
| `sat.audio.setMasterVolume(volume)` | Set gain on the entire mix. |

An entity has one voice at a time. Entity voices end on destruction, on stop, or
when their clip ends. Each paused voice and one-shot also occupies a slot. The
pool defaults to 64 mixed voices and zero virtual voices. When virtual slots
are enabled, higher priorities win mixed slots. Ties favor older voices.
Explicitly paused voices and voices suspended by time scale yield their mixed
slots to eligible voices. Virtual voices retain their playback cursor and
resume from it when promoted. They retain their decoder resources while
waiting. `isPlaying` includes virtual and time-scale-suspended voices that have
not been explicitly paused. A full pool reports an error.

Each bus plays in real time by default. **Follow Time Scale** multiplies that
bus's voice pitch by `sat.time.timeScale()`, with scaled pitch clamped to
0.01–16. A scale of zero suspends those voices at their current cursor.
Pitch and playback speed change together.

Audio API calls, asset reads, and scene updates run on the engine thread.
Miniaudio mixes on the device thread and streams from cached encoded bytes on
its resource worker. Select a clip in **Content** to preview it or set **Audio Import** to Auto,
Decode, or Stream. Auto decodes clips up to 256 KiB and streams larger clips
from memory. Decode caches PCM for the session. Stream decodes pages as needed.
The editor saves the mode in the clip's `.satimport` sidecar, which is included
when packaging the project. The encoded limit is
64 MiB per clip. The C++ null backend supports hardware-independent tests.

## Listening check

Copy `Engine/Audio/tests/fixtures/tone.wav` into a project's `Assets/Audio/`
directory. Add the source example above to an entity using the path
`Assets/Audio/tone.wav`. Set its spatial flag to `0` for the first check.
Start Play and listen for the repeating tone, then stop Play and confirm it
falls silent. For a spatial check, enable the spatial flag and move the entity
relative to the main camera. Build and launch GamePlayer for the same project
to check playback from the audio pak.
