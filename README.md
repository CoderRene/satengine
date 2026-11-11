# SatEngine

> **Work in progress.** APIs, project format, and editor behavior will change without notice.

A small C++20 game engine and editor shell. Built with CMake, GLFW, Direct3D 11, and ImGui (docking).

This repo is an early personal project, expect incomplete systems, rough edges, and breaking changes as things are explored and rewritten.

> Development is **AI-assisted**: tools like Cursor & Codex are used alongside human direction for design, implementation, and iteration. Treat the code as experimental.

## Status

SatEngine is **not production-ready**. Right now it is closer to a sandbox for learning and iterating on engine/editor pieces:

- Window / app loop (GLFW)
- D3D11 renderer with a few editor-oriented passes (meshes, selection outline, translate gizmo, camera frustum)
- Basic scene / project handling
- ImGui editor panels (hierarchy, inspector, content)

Still early or missing: polish, docs, tests, broader platform support, and a stable public API.

## Requirements

- Windows
- Visual Studio with the C++ desktop workload (CMake + MSVC)
- Git (dependencies are fetched via CMake `FetchContent`)

## Build and run

The default helpers build and launch the editor (`Editor` target, `SatEngine.exe`):

```powershell
.\scripts\build.ps1
.\scripts\run.ps1
```

The standalone host (`GamePlayer` target) runs embedded Lua game code through
the main scene camera in its own window. Select the project at build time; the
executable launches without arguments or access to the original project folder. It does not initialize or link ImGui, create
editor panels, or need editor fonts/icons. Use the isolated game-player preset:

```powershell
.\scripts\build.ps1 -Target GamePlayer -Configuration Release -ProjectFolder .\Examples\LuaCube
.\scripts\run.ps1 -Target GamePlayer -Configuration Release
```

Or use CMake directly:

```powershell
cmake --preset windows-gameplayer -DSAT_GAME_PROJECT="${PWD}/Examples/LuaCube"
cmake --build --preset windows-gameplayer-release
.\build-gameplayer\bin\Release\LuaCube.exe
```

`SAT_BUILD_EDITOR=OFF` skips downloading ImGui as well as compiling the editor.
The default `windows` preset enables both executables; build `--target Editor` or
`--target GamePlayer` to choose one. Debug and Release both support this separation.
The editor remains at `build/bin/<Configuration>/SatEngine.exe`;
a direct game-player-preset build writes `build-gameplayer/bin/<Configuration>/<project-folder-name>.exe`.
The game-player build helper installs to `<project>/Build/<project-folder-name>.exe`
and keeps intermediate files in `<project>/Build/.satengine-build/`.

To stage the standalone host outside the source tree:

```powershell
cmake --install build-gameplayer --config Release --prefix dist/SatGame
.\dist\SatGame\LuaCube.exe
```

The install contains the executable with the saved scene and project Lua scripts embedded.
`SAT_GAME_PROJECT` defaults to `Examples/LuaCube` and persists in the build cache.
Script edits, additions and removals update the bundle on the next build. A machine
running an MSVC build needs the matching Visual C++ runtime. Editor-authored scenes and Lua component attachments are embedded in the game.
General asset packaging is not implemented yet.
The editor supports **Build > Build Standalone**, which automatically writes
`<project>/Build/<project-folder-name>.exe` and shows background build status. General asset packaging is not implemented.

## Layout and dependencies

| Target / path | Role |
| --- | --- |
| `SatScene` | Portable scene types, math, camera and mesh extraction |
| `SatScripting` | Lua runtime and project-folder support, independent of graphics |
| `SatRuntime` | `SatScene` + `SatScripting` + window, runtime application loop, D3D11 renderer; no ImGui |
| `SatEditor` | `SatRuntime` + project tools, file dialogs, editor UI and ImGui backends |
| `Editor/` | Editor host owns its scene/project; produces `SatEngine.exe` |
| `GamePlayer/` | Standalone player host; links only `SatRuntime`; produces `<project-folder-name>.exe` |
| `Engine/Assets/` | Editor fonts/icons; copied only for the editor |
| `scripts/` | Build/run helpers, defaulting to the editor |

Subclass `sat::Application` in `GamePlayer/` to populate `scene()` in `onStart()` and
update it in `onUpdate(float deltaSeconds)`. The host handles events, framebuffer
resizing, scene extraction and window presentation. A missing main camera clears
the window without drawing scene meshes. Rendering helpers still support editor
debug overlays, but standalone draw data disables the grid, outlines, frustums
and gizmos. Only the editor Scene preview shows the reference grid; the Game
panel matches standalone rendering without a grid. Game frames reuse the offscreen renderer and copy to the swap-chain
back buffer without an ImGui presentation pass.

`UiLayer` borrows the editor host's scene/project and owns both ImGui backends.
Editor and game use the same `SceneRenderData` camera/mesh extraction code.

## Lua scripting

Entities support attached Lua script components with `onStart`, `onUpdate(deltaSeconds)`
and `onStop` callbacks. Add an empty entity in Hierarchy, then use Inspector >
Add Component to add mesh, camera and Lua script components. At runtime, scripts can
also add/remove mesh and camera, query `hasMesh` / `hasCamera`, and enable/disable
attached scripts. Unattached files never run automatically. Scenes and attachments
are saved in `Scene.sat`. Click Play/Stop in the editor, or run
`<project-folder-name>.exe`. Try `Examples/LuaCube`. See
[Lua scripting](docs/LuaScripting.md) for the API and lifecycle.

## Validation

Portable scene-render-data tests run without GLFW, ImGui, or D3D11, including on macOS:

```sh
cmake -S . -B /tmp/sat-scene-tests -DSAT_BUILD_GAME_PLAYER=OFF -DSAT_BUILD_EDITOR=OFF -DBUILD_TESTING=ON
cmake --build /tmp/sat-scene-tests
ctest --test-dir /tmp/sat-scene-tests --output-on-failure
```

On Windows, build both targets and run the same tests with
`ctest --test-dir build -C Debug --output-on-failure`. Smoke-test the game-only
Release install from a directory outside the repository, then verify resize,
minimize/restore, close, and the editor's scene/game views and panels.

## License

**TBD**, not decided yet while the project is still in flux.

Static OBJ meshes and PNG/JPEG textures are supported in Mesh components through
Inspector and Lua, including standalone archives. See [Asset loading](docs/AssetLoading.md)
and the `Examples/AssetLoading` project.
