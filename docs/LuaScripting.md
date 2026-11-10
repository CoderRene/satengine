# Entity components and Lua behaviors

Scripts run only when attached to an entity. Files in `Scripts/`, including
`main.lua`, never execute automatically. Helpers loaded by an attached script's
`require()` execute when required.

1. Open/create a project, then choose **Add Empty Entity** in Hierarchy (or its
   right-click menu). An empty entity has a Transform and no mesh, camera or scripts.
2. Select the entity. In Inspector, choose **Add Component** to add a **Mesh
   (Cube)**, **Camera**, or **Lua Script** from the project's `Scripts/` directory.
3. Click **Play**. Only enabled attached scripts receive callbacks. **Stop**
   restores the scene from before Play.

Inspector supports multiple different scripts on an entity, an Enabled checkbox
for each script, and removal of mesh/camera/script components. A script may be
attached once to each entity and reused on other entities. Transform is mandatory.
Place new `.lua` files under `Scripts/` using your code editor; reopen Add Component
to refresh the script list. Missing files are flagged in Inspector.

Each script component has its own Lua VM, globals, module cache and local state.
`self.entity` is its owning entity's one-based ID. Define optional global callbacks:

```lua
local angle = 0

function onStart()
    -- Initialize this component instance.
end

function onUpdate(deltaSeconds)
    angle = (angle + 45 * deltaSeconds) % 360
    sat.setRotation(self.entity, 0, angle, 0)
end

function onStop()
    -- Called once after this instance starts successfully.
end
```

Scripts start in entity/component order. A script's top-level code runs before
its onStart; then each instance receives updates in the same order. The active
component roster is fixed for a Play session. Editor changes are disabled during
Play. Restarting Play creates fresh instances. On error, updates stop, successful
instances receive onStop, and the editor restores the original scene. Startup
failure does not call onStop on the failing instance. The first error is retained;
tracebacks identify the owning entity and script. Window-thread use only.

`require("movement")` loads `Scripts/movement.lua`; dotted module names and
`Scripts/module/init.lua` are supported. Built games load these modules from the
embedded bundle without filesystem fallback.

| Function | Behavior |
| --- | --- |
| `sat.createEntity(name)` | Create an empty entity and return its ID |
| `sat.createCube(name)` | Create a cube and return its ID |
| `sat.createCamera(name)` | Create a camera and return its ID |
| `sat.findEntity(name)` | Return the first matching ID, or nil |
| `sat.getPosition(id)` / `sat.setPosition(id, x, y, z)` | Read three numbers / write position |
| `sat.getRotation(id)` / `sat.setRotation(id, x, y, z)` | Read/write Euler rotation in degrees |
| `sat.getScale(id)` / `sat.setScale(id, x, y, z)` | Read/write scale |
| `sat.setMainCamera(id)` | Select an entity with a camera as the main camera |

Transforms use the engine's left-handed coordinates (+Y up, +Z forward); scale
is dimensionless. Entity IDs survive creation but not removal/reordering or a
new Play session. Scripts cannot remove entities or attach components at runtime.
Invalid IDs, nonfinite numbers and invalid callbacks produce Lua errors.

## Saving and building

**File > Save Scene** saves entities, transforms, mesh/camera components and
script attachments to the project's versioned `Scene.sat` file. The editor also
saves before Play, Build, switching projects and normal shutdown. A malformed
scene is reported rather than overwritten. Older projects without Scene.sat
start with a default camera and no attached scripts; add attachments and save.

Choose **Build > Build Standalone**. Builds go to
`<project>/Build/<project-folder-name>.exe`; `Build/build.log` contains the build
output and `Build/.satengine-build/` holds reusable intermediate files. Build
Status shows live logs, elapsed time and a stage-based percentage (prepare,
configure/download, compile, package), not an exact compilation/time estimate.
The first build downloads dependencies and compiles the engine. Builds require
the engine checkout and Visual Studio's C++/CMake toolset. Stop Play before building.
An active build continues if the editor closes.

Command-line equivalents:

```powershell
.\scripts\build.ps1 -Target GamePlayer -Configuration Release -ProjectFolder "C:\path\to\MyGame"
.\scripts\run.ps1 -Target GamePlayer -ProjectFolder "C:\path\to\MyGame"
```

The default project is `Examples/LuaCube`. A direct CMake build can select another
project using `-DSAT_GAME_PROJECT="C:/path/to/project"`. Scene.sat and all project
Lua files are embedded, so the named executable runs without the original project
folder. Rebuild after scene/script changes. General asset packaging is not yet
implemented; explicit `io`, `dofile` and `loadfile` calls still access disk.

The Windows game opens no console or error dialogs. Errors and Lua tracebacks go
to `SatGame.log` beside the executable; if unwritable, it uses
`%LOCALAPPDATA%/SatEngine/Logs/SatGame.log` (then a temporary-folder fallback).
The log is replaced each launch and can be empty on success. The editor displays
script errors in Lua Errors and scene errors in Scene Errors.

Lua 5.4.9 is statically built from checksum-pinned source. Project scripts are
trusted code with standard-library filesystem/process access; this is not a
security sandbox. Each callback has a one-million-instruction limit to catch
Lua loops; blocking native calls are not interrupted. Destruction releases VMs;
hosts call stop() explicitly to run onStop callbacks.

## Runtime input

Input comes from GLFW window events independently of ImGui. The standalone game
and editor Play expose the same window input. Editor UI interactions also reach
scripts during Play; cursor coordinates are relative to the whole client window,
not the embedded Game view. There is no viewport capture/filtering yet.

```lua
function onUpdate(dt)
    local x, y, z = sat.getPosition(self.entity)
    if sat.input.keyHeld(sat.Key.W) then z = z + 5 * dt end
    if sat.input.keyPressed(sat.Key.Space) then y = y + 1 end
    if sat.input.mouseReleased(sat.MouseButton.Left) then
        local cursorX, cursorY = sat.input.cursorPosition()
        print(cursorX, cursorY)
    end
    sat.setPosition(self.entity, x, y, z)
end
```

`sat.input.keyPressed/keyHeld/keyReleased(key)` and
`sat.input.mousePressed/mouseHeld/mouseReleased(button)` return booleans.
`sat.input.cursorPosition()` returns x, y in GLFW screen units (top-left origin,
+X right, +Y down; not framebuffer pixels). Key constants include A–Z,
Digit0–Digit9, arrows, Space, Escape, Enter, modifiers, F1–F25 and keypad keys.
Mouse constants are Left, Right, Middle and Button4–Button8. Key names follow
GLFW's key tokens, not text entry or keyboard-layout characters.

Pressed/released edges last for one event-poll frame; held lasts until release.
A quick tap can report both edges in the same frame. Repeat does not retrigger
pressed. Queries never consume events, so all script components see the same
state. Focus loss releases all held keys/buttons. Hosts must advance input once
before polling events and updating scripts; all input use is on the window thread.
C++ hosts may pass a borrowed `const Input*` to `LuaRuntime`; it must outlive the
runtime. Without one, scripts receive idle input and cursor (0, 0).
