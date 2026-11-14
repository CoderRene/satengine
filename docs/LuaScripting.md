# Entity components and Lua behaviors

Scripts run only when attached to an entity. Files in `Scripts/`, including
`main.lua`, never execute automatically. Helpers loaded by an attached script's
`require()` execute when required.

1. Open/create a project, then choose **Add Empty Entity** in Hierarchy (or its
   right-click menu). An empty entity has a Transform and no mesh, camera or scripts.
2. Select the entity. In Inspector, choose **Add Component** to add a **Mesh
   (Cube)**, **Camera**, or **Lua Script** from the project's `Scripts/` directory.
3. Click **Play**. Enabled attached scripts run in the Game view's copy of the
   scene. Entity edits from Scene, Hierarchy, and Inspector also update that
   running copy. When you click
   **Stop**, choose **Save Changes** to write your editor edits to Scene.sat or
   **Discard Changes** to restore the scene from before Play.

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

### Inspector fields

The [Lua Inspector Fields example](../Examples/LuaInspectorFields/) shows two
entities using the same script with different values.

Put `sat.expose { ... }` at the start of an attached project script, after any
header comments. It declares editable defaults and makes the values available
on `self` as soon as the declaration runs.

```lua
sat.expose {
    speed = 45.0,
    spinning = true,
}

---@class SpinSelf: ScriptSelf
---@field speed number
---@field spinning boolean
local cube = self --[[@as SpinSelf]]

local angle = 0

function onUpdate(deltaSeconds)
    if cube.spinning then
        angle = (angle + cube.speed * deltaSeconds) % 360
        sat.setRotation(cube.entity, 0, angle, 0)
    end
end
```

Select the entity and expand its **Lua Script** component to edit the fields.
Each attachment can store its own overrides in the scene. **Reset** returns a
field to its script default. Edits during Play take effect in the running
instance before the next update. The usual **Save Changes** or **Discard
Changes** prompt applies when you stop. Read fields from `self` or its alias
in your callbacks so they pick up live edits.

Save the Lua file to refresh its fields in the open Inspector. If a save has
an error, the Inspector shows the error alongside the last valid fields until
you save a correction.
Fields appear in the order you write them in the `sat.expose { ... }` block.

Lua Language Server uses the `SpinSelf` annotations above for completion and
type checking. Give each script's fields a class that inherits `ScriptSelf`
to keep its fields scoped to that script.

Use boolean, integer, finite number, or single-line string defaults. Write
`45.0` for a decimal field and `45` for an integer field. Names use identifier
characters, and `entity` is reserved. Field declarations may contain up to 128
fields with text values up to 4096 bytes. Keep the declaration self-contained
so the Inspector can read it with engine APIs and standard libraries disabled.
Restart Play after changing a declaration in the source file to load the new
defaults or types into the running instance. Saved values with a different type
are ignored until replaced or reset.

#### Entity references

```lua
sat.expose {
    target = sat.entityField(),
}

function onUpdate()
    local target = self.target
    if target and sat.isEntityValid(target) then
        local x, y, z = sat.getPosition(target)
        sat.setPosition(self.entity, x, y + 2, z)
    end
end
```

Select the entity that owns the script, then drag a row from **Hierarchy** onto
the `target` field in the Inspector. Lua receives the assigned entity's ID, or
`nil` when the field is empty. **Reset** clears the assignment. The reference
is saved with the scene and can be reassigned during Play. Check
`sat.isEntityValid` before using a target that may have been destroyed.

Scripts start in entity/component order. A script's top-level code runs before
its onStart; then each instance receives updates in the same order. The active
component roster is fixed for a Play session, except that destroyed owners retire
their components. Newly created entities do not automatically run scripts.
Restarting Play creates fresh instances from the authored scene. On error,
updates stop, successful instances receive onStop, and pending editor edits
still prompt for a save decision. Startup
failure does not call onStop on the failing instance. The first error is retained;
tracebacks identify the owning entity and script. Window-thread use only.

`require("movement")` loads `Scripts/movement.lua`; dotted module names and
`Scripts/module/init.lua` are supported. Built games load these modules from the
in-memory bundle without filesystem fallback (compiled bytecode from the standalone pak).

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
| `sat.getPosition(id)` / `sat.setPosition(id, x, y, z)` | Read three numbers / write double-precision world position |
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

Transforms use the engine's left-handed coordinates (+Y up, +Z forward). Entity
position is double-precision world space; rotation is Euler degrees and scale
is dimensionless float. Entity IDs are opaque positive Lua integers, not array indices.
All entity APIs, including camera selection and `self.entity`, use these IDs. Creation, vector
growth, and destruction/reordering of other entities preserve surviving IDs.
Destroyed IDs never identify a replacement entity. Do not perform arithmetic on
IDs or persist them in project files: scene loads create fresh runtime IDs.
Scene copies used for editor Play preserve existing entity identities. Stop
discards the runtime copy, including entities created by scripts.

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
feed the next Game view render. Editor saves use the authored scene.

```lua
if not sat.hasMesh(self.entity) then
    sat.addMesh(self.entity)
end
sat.setScriptEnabled(sat.findEntity("Target"), "target.lua", false)
```

## Saving and building

The top bar shows an amber **Unsaved changes** indicator after scene edits.
A successful save clears it. During Play, the indicator tracks editor edits,
and the save prompt appears when Play ends with pending edits.

**File > Save Scene** saves entities, transforms, mesh/camera components and
script attachments to the project's versioned `Scene.sat` file. The editor also
saves before Play, Build, switching projects and normal shutdown. A malformed
scene is reported rather than overwritten. Older projects without Scene.sat
start with a default camera and no attached scripts; add attachments and save.

Choose **Build > Build Standalone**. The editor opens a folder picker for the
optional private obfuscator: select the folder containing `generate.py`.
Cancel the picker to build without obfuscation; encryption stays enabled.
A selected folder without the tool also uses the fallback. The selection is passed
only to the build configuration, not saved in project files. Builds go to
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
Lua scripts are precompiled by `SatPakBuilder` using the player's pinned Lua
version. The build produces `<project-folder-name>.exe` and an encrypted archive
set: `<project-folder-name>.pak`, then `.1.pak`, `.2.pak`, and so on as needed.
Ship the executable and every current part together. The player locates the pak beside its executable,
independently of the working directory. Missing/invalid pak errors go to the log.

The versioned pak contains `Scene.sat`, compiled scripts under `Scripts/` (the
`.lua` paths are retained for attachments and `require`), and every file under
`Assets/`, including textures and audio. Assets are packed unchanged. C++
`PakArchive::find` returns borrowed binary bytes from memory; texture decoding
and audio playback are not introduced by packaging. Lua `io`, `dofile` and
`loadfile` still access disk, rather than transparently reading the pak.

The engine validates archive sizes, paths, duplicate names and format version,
then loads the archive into memory once. Entry scripts and required modules are
loaded with Lua's bytecode-only mode. Bytecode retains debug locations for
tracebacks and is specific to the Lua version/build architecture; rebuild the pak
with its player. Rebuild after scene, script or asset changes. Syntax errors in
any project script fail the build. Editor Play continues to load source files.
Each physical part is at most 2 GiB including encryption overhead. The logical
archive stream splits across parts automatically, including files larger than a
single part. The old 100000-entry cap is removed; the format stores a 32-bit entry
count. Sets are limited to 4096 parts and available address space. The packer and
loader currently hold the logical archive in memory; splitting does not provide
asset streaming or reduce total RAM requirements.

AES-256-GCM encrypts paths and payloads and authenticates part metadata. Fresh
random nonces and a random set ID prevent accidental generation mixing. Missing,
wrong-key, damaged, reordered or mixed-set parts fail loading before Lua runs.
There is no plaintext-format fallback. Archives remain uncompressed.

Independent random 256-bit keys are generated for scripts/scene, textures,
audio, and other assets. The primary `.pak` holds scripts and scene; additional
`<name>.textures.pak`, `<name>.audio.pak`, and `<name>.assets.pak` groups are always
emitted, even when empty. Each group splits at 2 GiB. Ship all current files listed
in the generated `.pak.parts` build manifest; the manifest itself is not required
by the player. CMake stages and installs all current groups and parts.

Keys and generated reconstruction code live outside the checkout and project.
The default private root is `~/Desktop/SatEnginePrivate` (`%USERPROFILE%/Desktop/SatEnginePrivate` on Windows),
overridable with `SAT_PRIVATE_ROOT` environment variable or CMake cache path.
The optional private tool is `<private-root>/generate.py`; its implementation is
not part of this repository. It accepts `--project`, `--release`, and `--output`,
then emits `scripts.bin`, `textures.bin`, `audio.bin`, `assets.bin` (32 bytes each)
and `GamePakKeys.cpp` implementing the public `GamePakKeys.hpp` contract.

If the private tool exists, the build uses it. If absent, the public fallback
`scripts/generate_release_keys.py` emits an unobfuscated provider and the build
continues with encryption enabled. CMake reports the selected mode. A present but
failing private tool fails the build rather than silently downgrading. Private
tools, generated code, and key files are neither installed nor bundled.

Each standalone export via `build.ps1` generates a fresh public release ID unless
`SAT_RELEASE_ID` is supplied in the environment. Direct CMake builds cache a
release ID for incremental builds; pass `-DSAT_RELEASE_ID=<new-id>` to rotate it.
Generated outputs are isolated by project/build/release. Deploy the matching
player and complete archive set together. Runtime key buffers are securely cleared
after each group is loaded, including failure paths. Windows uses BCrypt;
portable tests require OpenSSL 3 Crypto. Python 3 is required for standalone key
provider generation. Keep private files outside source control and restrict
access to the private root using host filesystem permissions.
Encryption deters casual inspection; a determined reader can extract a key from
the distributed player. It is not DRM or protection against a compromised client.
Rebuild old v1 archives; they are no longer accepted.

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

### Time helpers

`sat.time` is shared by all script components in one Play session, in both the
editor and standalone player. All durations are seconds:

- `sat.time.deltaSeconds()` — the scaled frame delta, matching `onUpdate(deltaSeconds)`.
- `sat.time.elapsedSeconds()` — accumulated scaled frame deltas since Play began.
- `sat.time.unscaledDeltaSeconds()` — the frame delta supplied by the host.
- `sat.time.unscaledElapsedSeconds()` — accumulated host deltas since Play began.
- `sat.time.timeScale()` — current scale (default `1`).
- `sat.time.setTimeScale(scale)` — sets a finite, nonnegative float scale;
  `0` pauses game time, `0.5` gives half speed, and `2` gives double speed.

Scale changes apply on the next frame. Every component sees the same frame delta
and elapsed time, including components that were disabled on earlier frames.
Updates still run at scale `0`, allowing scripts to resume time using input or
unscaled time. Before the first update, delta and elapsed values are `0`.
Starting Play again resets all time values and restores scale `1`. Elapsed time
counts supplied frame deltas, rather than wall-clock time spent loading scripts.

```lua
function onUpdate(deltaSeconds)
    if sat.input.keyPressed(sat.Key.Space) then
        sat.time.setTimeScale(sat.time.timeScale() == 0 and 1 or 0)
    end
    local animationTime = sat.time.elapsedSeconds()
end
```

## Game UI

Put RML layouts, RCSS stylesheets, fonts and PNG or JPEG images under
`Assets/UI/`. Lua uses project-relative paths. RML and RCSS can reference
files relative to their own location. The same paths work from disk in editor
Play and from the pak in GamePlayer. During editor Play, saves to loaded `.rml` files
and their linked `.rcss` stylesheets take effect within about 300 ms. Stylesheet
edits update the existing document in place, preserving focus, form values, and
Lua state. RML edits replace the document's markup while preserving the model
values and Lua VMs. Document handles survive and element handles and listeners
follow matching IDs. Removed IDs invalidate their handles and listeners. An RML
parse error leaves the last loaded layout visible. Restart Play to pick up font
or image changes.

Copy `Engine/Assets/Fonts/JetBrainsMono.ttf` into the project's `Assets/UI/`.
Save this layout as `Assets/UI/hud.rml`.

```rml
<rml>
<head>
    <link type="text/rcss" href="hud.rcss" />
</head>
<body>
    <button id="action"><span id="count">Clicks 0</span></button>
</body>
</rml>
```

Save this stylesheet as `Assets/UI/hud.rcss`.

```css
body {
    font-family: "JetBrains Mono";
    font-size: 18px;
    color: white;
    pointer-events: none;
}

#action {
    position: absolute;
    top: 24px;
    left: 24px;
    padding: 12px 20px;
    background-color: #273752;
    pointer-events: auto;
}

#action:hover {
    background-color: #3a5278;
}
```

Attach this script to an entity. In Play, click the button in the Game view to
increment the counter.

```lua
local ui = sat.ui
local doc, listener
local clicks = 0

function onStart()
    assert(ui.loadFont("Assets/UI/JetBrainsMono.ttf"))
    doc = assert(ui.load("Assets/UI/hud.rml"))
    local button = assert(ui.element(doc, "action"))
    local count = assert(ui.element(doc, "count"))
    listener = assert(ui.on(button, "click", function(event)
        clicks = clicks + 1
        ui.setText(count, "Clicks " .. clicks)
    end))
    ui.show(doc)
end

function onStop()
    if listener then ui.off(listener) end
    if doc then ui.close(doc) end
end
```

`ui.load` returns a hidden document or `nil, error`. Use `show` and `hide`
to control visibility. Closing a document invalidates its handles and removes
its listeners. `ui.element` and `ui.getValue` return `nil` for an invalid
handle. Document and element setters return whether the operation succeeded.

Use `setText` for escaped text, `setRml` for markup, `setClass` to toggle an
RCSS class, `setProperty` and `removeProperty` for inline styles, and
`setAttribute` for attributes. `getValue` returns a form control's current
value or the element's `value` attribute.

`ui.on(element, eventName, callback)` returns a listener ID for `ui.off`.
Callbacks run in the VM that registered them, after queued messages and before
`onUpdate`. The event table contains `type`, viewport-local `x` and `y` in
pixels, the RmlUi `key` identifier, and `value`. Use `ui.wantsMouse()` and
`ui.wantsKeyboard()` to skip game actions while the UI has hover or focus.
In editor Play, mouse input goes to the Game image and keyboard input is
available when that image is hovered or its window has focus.

### Data models

Create the model before loading a document that references it. Fields are
booleans, finite numbers or strings. Model names are shared across the UI
context. The VM that creates a model owns its lifetime.

```lua
assert(sat.ui.createModel("hud", {score = 0, pilot = ""}))
assert(sat.ui.setModelValue("hud", "score", 25))
local score = sat.ui.getModelValue("hud", "score")
```

```rml
<div data-model="hud">
    <p>Score {{score}}</p>
    <input type="text" data-value="pilot" />
</div>
```

`data-value` writes edits back to the model. `getModelValue` returns the
current value or `nil` for an unknown model or field. Changes are applied to
the layout during the UI update.

### Scene UI components

In the Inspector, choose **Add Component > Game UI**, then select an RML
document and an optional font from `Assets/UI/`. **Show on Start** controls
initial visibility. The host loads the document after Lua startup. A script
can call `sat.ui.getDocument(self.entity)` after creating its model to load
and access its authored document during `onStart`. Keep the document's font
family in RCSS consistent with the selected font. Text inputs require a
loaded font.

### RCSS effects

The D3D11 backend supports color filters, opacity, blur, drop shadows,
backdrop filters, clip masks, and `mask-image`. Use linear, radial or conic
gradients in decorators and masks. Gradients support up to 64 stops. Blur
sigma is capped at 64 pixels.

Use `decorator: shader("Assets/UI/stripe.slang");` for a project shader.
Its `psMain` takes `SV_Position`, `COLOR` and `TEXCOORD0`. The UV spans
the decorator box from top left to bottom right. Return premultiplied RGBA,
including the element opacity carried in `COLOR.a`. The Debug editor compiles the shader through the project shader pipeline.
GamePlayer loads its packaged bytecode.

## Finding and controlling other entities

Use `sat.findEntity(name)` for a uniquely named entity, or
`sat.findEntities(name)` for a snapshot array of every live exact-name match.
Both use current scene order; duplicate names are allowed and deletion can change
that order. Cache the returned stable IDs rather than searching every frame.
Check `sat.isEntityValid(id)` before using a cached ID after destruction. A new
entity with the same name has a different ID; reacquire it explicitly if needed.
IDs remain valid across entity creation, renaming and unrelated deletion, but
are runtime identities and should not be saved as persistent scene references.

Any script can drive another entity through the same transform and component
APIs it uses for `self.entity`:

```lua
local door
function onStart()
    door = sat.findEntity("Door")
end
function onUpdate(dt)
    if door and sat.isEntityValid(door) then
        local x, y, z = sat.getPosition(door)
        sat.setPosition(door, x, y + dt, z)
    end
end
```

### Sending messages to another script

`sat.sendMessage(entity, path, message, payload)` queues a message for one script
component, identified by stable entity ID and its path relative to `Scripts/`.
It returns `true` when queued, or `false` for an invalid/destroyed entity, disabled
component, or script outside the current Play roster. A missing `onMessage`
handler silently ignores delivery. A `true` result acknowledges queuing, not
completion; send a reply if the caller needs confirmation.

The optional payload is nil, a boolean, a finite number, or a string. Values are
copied between VMs; Lua integers retain their precision, and strings may include
zero bytes. Tables, functions, threads and userdata are rejected. Script globals
and `require` caches remain private to each component.

```lua
-- Controller.lua
local door
function onStart()
    door = sat.findEntity("Door")
    if door then
        assert(sat.sendMessage(door, "Door.lua", "open", 3))
    end
end

-- Door.lua (attached to Door)
local targetHeight = 0
function onMessage(sender, message, payload)
    if message == "open" and type(payload) == "number" then
        targetHeight = payload
    end
end
function onUpdate(dt)
    local x, y, z = sat.getPosition(self.entity)
    sat.setPosition(self.entity, x, math.min(targetHeight, y + dt), z)
end
```

Delivery rules:

- Messages run in send order at the next update boundary, before any `onUpdate`,
  after all startup callbacks have completed. Startup code can message a script
  later in the initial roster. Messages also run when time scale is zero.
- Replies and other messages sent during delivery wait for the following update.
  Callbacks never recursively enter another VM. Each frame uses a fixed batch.
- A recipient destroyed, disabled or detached before delivery is skipped. Queued
  messages are discarded on Stop or restart. Components disabled at Play startup
  have no VM, even if later enabled. New attachments wait for the next Play.
- A sender may be destroyed before delivery; its ID is still passed to the
  receiver. Validate it before sending a reply or accessing its components.
- Handler errors, nonfunction `onMessage` values and instruction-budget failures
  stop the session with the recipient's entity/script context and traceback.
- The pending queue is bounded to 1024 messages and 1 MiB of message-name/payload
  string data. Names contain 1–256 bytes. Invalid arguments and queue overflow
  raise Lua errors; use `pcall` when recovery is needed.

Choose one script to own an entity's movement when several scripts communicate
with it. Direct mutations are immediate; multiple writes follow callback order.

## Basic math helpers

`sat.math` complements Lua's standard `math` library with engine rotation
conventions and common movement/vector operations. Coordinates are left-handed:
+X right, +Y up, +Z forward. Angles are degrees. Use Lua `math.rad`, `math.deg`,
`math.sin`, `math.cos`, etc. for ordinary scalar math; Lua trigonometry takes
radians.

Vectors are passed as consecutive `x, y, z` numbers and returned as three
numbers, matching the transform API. Helpers create no Lua vector tables or
userdata. Scalar/vector calculations use doubles; object rotations use the same
float matrix as the engine renderer. All inputs must be finite; invalid bounds,
negative movement limits, and nonfinite results raise Lua errors.

| Helper | Behavior |
| --- | --- |
| `sat.math.clamp(value, minimum, maximum)` | Clamp to inclusive bounds; minimum must not exceed maximum |
| `sat.math.lerp(from, to, t)` | Linear interpolation; clamps `t` to `[0, 1]` |
| `sat.math.moveTowards(current, target, maxDelta)` | Move by at most nonnegative `maxDelta`, without overshooting |
| `sat.math.deltaAngle(from, to)` | Signed shortest degree difference in `(-180, 180]`; exact half-turns choose `+180` |
| `sat.math.lerpAngle(from, to, t)` | Interpolate along the shortest degree arc; clamps `t` to `[0, 1]`, returns `[0, 360)` |
| `sat.math.length(x, y, z)` | Vector magnitude |
| `sat.math.distance(ax, ay, az, bx, by, bz)` | Distance between two points |
| `sat.math.dot(ax, ay, az, bx, by, bz)` | Vector dot product |
| `sat.math.cross(ax, ay, az, bx, by, bz)` | Vector cross product; `up × forward = right` |
| `sat.math.normalize(x, y, z)` | Unit vector; a zero vector returns `(0, 0, 0)` |
| `sat.math.rotateVector(x, y, z, pitch, yaw, roll)` | Rotate a vector using object Euler degrees, `Rz * Ry * Rx`; ignores translation and scale |
| `sat.math.forward(pitch, yaw, roll)` | Object's rotated local `+Z` axis, using the same Euler matrix |
| `sat.math.cameraForward(pitch, yaw)` | Camera view direction; roll is ignored, matching the engine view matrix |

The engine currently has distinct object and camera pitch conventions. For
objects, positive pitch rotates `+Z` toward `-Y`; positive yaw turns `+Z` toward
`+X`, and positive roll turns `+X` toward `+Y`. Cameras use positive pitch to look
up (`+Y`) and positive yaw to look toward `+X`. Use `forward` for object movement
and `cameraForward` for camera movement. Rotation arguments must fit a finite
float, matching `sat.setRotation`.

```lua
function onUpdate(dt)
    local pitch, yaw, roll = sat.getRotation(self.entity)
    local dx, dy, dz = sat.math.forward(pitch, yaw, roll)
    local x, y, z = sat.getPosition(self.entity)
    local speed = 5
    sat.setPosition(self.entity,
        x + dx * speed * dt, y + dy * speed * dt, z + dz * speed * dt)
end
```

For smooth turning through zero, use
`sat.math.lerpAngle(350, 10, 0.5)` (returns `0`), rather than scalar `lerp` (returns
`180`). For constant-speed movement, pass `speed * dt` as `moveTowards`'s
`maxDelta`. Normalization handles very small and very large finite vectors
without inventing a direction for zero input. Arithmetic whose result cannot be
represented as a finite double raises an error.

## Mesh and material assets

Mesh components can use static OBJ files and shared SATMAT materials. Assign them with
`sat.setMeshAsset(id, "Assets/Meshes/model.obj")` and
`sat.setMaterialAsset(id, "Assets/Materials/Color.satmat")`. Use nil to reset either
assignment. Both APIs require a live entity with a Mesh component. Read paths
with `sat.getMeshAsset(id)` / `sat.getMaterialAsset(id)`.

See [Asset loading](AssetLoading.md) for formats, coordinate conversion, Inspector
controls, reload behavior and packed-game support. `Examples/AssetLoading`
provides a complete project.
