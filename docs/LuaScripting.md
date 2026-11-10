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
`self.entity` is its owning entity's stable ID. Define optional global callbacks:

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
component roster is fixed for a Play session, except that destroyed owners retire
their components. Newly created entities do not automatically run scripts. Editor
changes are disabled during Play. Restarting Play creates fresh instances. On error, updates stop, successful
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
| `sat.findEntity(name)` | Return the first live matching ID, or nil |
| `sat.destroyEntity(id)` | Destroy immediately; return false for an invalid/already destroyed ID |
| `sat.isEntityValid(id)` | Return whether this ID identifies a live entity in this scene |
| `sat.getName(id)` | Return the live entity's name |
| `sat.getEntities()` | Return a new array containing the current live IDs |
| `sat.getEntityCount()` | Return the number of live entities |
| `sat.getPosition(id)` / `sat.setPosition(id, x, y, z)` | Read three numbers / write position |
| `sat.getRotation(id)` / `sat.setRotation(id, x, y, z)` | Read/write Euler rotation in degrees |
| `sat.getScale(id)` / `sat.setScale(id, x, y, z)` | Read/write scale |
| `sat.setMainCamera(id)` | Select an entity with a camera as the main camera |
| `sat.getMainCamera()` | Return the stable main-camera entity ID, or nil |
| `sat.getCameraFov(id)` / `sat.setCameraFov(id, degrees)` | Read/write vertical FOV in degrees, strictly between 0 and 180 |
| `sat.getCameraClipPlanes(id)` / `sat.setCameraClipPlanes(id, near, far)` | Read two numbers / atomically set positive near and far greater than near |
| `sat.getCameraClearColor(id)` / `sat.setCameraClearColor(id, r, g, b, a)` | Read/write four finite RGBA floats |
| `sat.hasMesh(id)` / `sat.hasCamera(id)` | Return whether the live entity has that component |
| `sat.addMesh(id)` / `sat.addCamera(id)` | Add the component if missing; return true on add, false if already present |
| `sat.removeMesh(id)` / `sat.removeCamera(id)` | Remove the component if present; return true on remove, false if absent |
| `sat.isScriptEnabled(id, path)` | Return whether that attached script path is enabled (false if not attached) |
| `sat.setScriptEnabled(id, path, enabled)` | Enable/disable an attached script; return true if found, false if not attached |

Transforms use the engine's left-handed coordinates (+Y up, +Z forward); scale
is dimensionless. Entity IDs are opaque positive Lua integers, not array indices.
All entity APIs, including camera selection and `self.entity`, use these IDs. Creation, vector
growth, and destruction/reordering of other entities preserve surviving IDs.
Destroyed IDs never identify a replacement entity. Do not perform arithmetic on
IDs or persist them in project files: scene loads create fresh runtime IDs.
Scene copies used for editor Play snapshots preserve identities; Stop restores
the authored snapshot, and runtime-created entities disappear.

`getEntities()` returns a snapshot, so it is safe to iterate that array while
creating or destroying entities. Storage/enumeration order may change after a
deletion; duplicate names are allowed, so keep IDs instead of relying on names
or list positions. The array itself does not keep entities alive. `getName`,
transform functions, component queries/mutations and `setMainCamera` reject
stale/invalid IDs with Lua errors. `destroyEntity` and `isEntityValid` return
false for invalid integer IDs; noninteger arguments produce Lua errors.
Nonfinite numbers and invalid callbacks also produce Lua errors. Scripts can
add/remove mesh and camera at runtime. Disabling a script skips its `onUpdate`
while keeping its VM state; re-enabling resumes it. Script paths are relative
to `Scripts/` (for example `target.lua`). Newly attached scripts during Play
still do not start until the next Play session.

```lua
local projectile = sat.createCube("Projectile")
sat.setPosition(projectile, 0, 1, 0)
print(sat.getName(projectile), sat.getEntityCount())
for _, id in ipairs(sat.getEntities()) do
    if sat.getName(id) == "Projectile" then
        sat.destroyEntity(id)
    end
end
assert(not sat.isEntityValid(projectile))
```

Destruction is immediate: the entity disappears from lookup, enumeration and
rendering, and its handle becomes invalid. The callback that destroys it may
finish, but must check validity before accessing that entity again. Its started
script components receive `onStop()` once at the next callback boundary, before
any further gameplay callbacks. `self.entity` is already invalid inside that
`onStop`; use `sat.isEntityValid` before accessing it. `onStop` can clean up other
live entities and can itself destroy other scripted owners. Unstarted components
on destroyed entities are skipped entirely. A top-level chunk that destroys its
owner finishes without `onStart`, then receives `onStop` if the chunk succeeded.
Stop errors preserve the existing first-error policy and halt updates.

For C++ hosts, `Entity::id()`, `Scene::entityById`, `destroyEntity`,
`setMainCameraById` and `entityCount` expose the same lifecycle. Lookup and removal
are average constant time; destruction swaps the last entity into the removed
slot. `entity(index)` remains a temporary editor/serialization index API;
`entityIndex(id)` resolves a live handle to its current optional index. Borrowed
entity references/pointers and indices must be reacquired after structural scene
changes. All scene/runtime operations run on the window thread.

## Camera scripting

```lua
local camera = sat.getMainCamera()
if camera then
    sat.setCameraFov(camera, 75)
    sat.setCameraClipPlanes(camera, 0.1, 2000)
    sat.setCameraClearColor(camera, 0.08, 0.12, 0.2, 1)
end
```

Camera property functions require a live entity with a Camera component.
Invalid handles, missing cameras, nonfinite/out-of-float-range numbers and invalid
projection ranges produce Lua errors. Setters validate every argument after float
conversion before changing any field; errors leave the previous settings intact.
Near/far distances use scene units. RGBA values are not implicitly clamped,
matching scene-file validation; use [0, 1] for ordinary display colors.

`createCamera` and `addCamera` do not select a main camera. Use `setMainCamera`
to switch; `getMainCamera` returns nil when no live camera is marked main,
including after that camera is removed or its entity is destroyed. Property
reads/writes use direct component access through stable-ID lookup and create no
Lua tables. Main-camera queries use the scene's existing linear scan. Changes
feed the next render-data update and are included when the host saves the scene;
editor Stop restores its Play snapshot.

```lua
if not sat.hasMesh(self.entity) then
    sat.addMesh(self.entity)
end
sat.setScriptEnabled(sat.findEntity("Target"), "target.lua", false)
```

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
