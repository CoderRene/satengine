# Audio Demo

Open this folder as a project in the editor, press **Play**, and focus the game
view. A cube moves from left to right while playing a looping hum. Use stereo
output to hear it pan as it passes the main camera, which acts as the listener.

| Key | Action |
| --- | --- |
| Space | Pause or resume the moving speaker's loop. |
| Enter | Play a spatial chime at the cube's current position. |
| M | Mute or unmute the music bus. The chime plays through the SFX bus. |
| T | Toggle half speed. The hum slows down and the chime plays in real time. |

**Stop** releases all voices. Press **Play** again to reset the controls.
The project includes both generated WAV clips and works as a GamePlayer project
with `SAT_GAME_PROJECT` set to this folder. See [Audio](../../docs/Audio.md)
for the scene format and Lua API.
