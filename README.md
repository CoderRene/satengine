# SatEngine

A small **C++20** game engine for **space games** — with an ImGui editor and a standalone game player.

A large-world or open-world space game engine. Design choices assume vacuum, local gravity fields, and spaceflight-style sims (rigid bodies, spatial audio, Lua-driven ships and stations). Built with CMake, GLFW, Direct3D 11, and Lua. Windows only for now.

> **Work in progress** — APIs and formats will change. This is an experimental, AI-assisted personal project, not production software.

## What's included

| Area | What you get |
| --- | --- |
| **Editor** | Hierarchy, Inspector, Content browser, Scene/Game views, Play/Stop |
| **Scripting** | Lua components with scene, input, time, physics, and audio APIs |
| **Rendering** | D3D11 meshes, textures, selection outline, gizmos, collider debug |
| **Assets** | Static OBJ meshes, PNG/JPEG textures, import settings |
| **Physics** | Jolt rigid bodies, compound colliders, local gravity (no global “down”) |
| **Audio** | WAV / Ogg / MP3 / FLAC, spatial sources, SFX/Music/UI buses |
| **Shipping** | Build a standalone `.exe` + `.pak` from **Build > Build Standalone** |

Forward PBR [lighting and materials](docs/Lighting.md) with [Slang shaders](docs/Shaders.md).

Still planned: animation, macOS/Linux editor, and a stable public API.

## Try an example

Open any folder under `Examples/` as a project in the editor, then press **Play**.

| Example | Try this if you want… |
| --- | --- |
| [LuaCube](Examples/LuaCube) | A minimal Lua-driven scene |
| [AssetLoading](Examples/AssetLoading) | Imported meshes and textured materials |
| [LightingDemo](Examples/LightingDemo) | Directional, point, and spot lights with metallic-roughness materials |
| [SpacePhysics](Examples/SpacePhysics) | Rigid bodies, gravity, triggers |
| [AudioDemo](Examples/AudioDemo) | Spatial audio, buses, time scale |

## Requirements

- Windows
- Visual Studio (C++ desktop workload — CMake + MSVC)
- Git (dependencies download via CMake `FetchContent`)

## Quick start

Build and launch the editor:

```powershell
.\scripts\build.ps1
.\scripts\run.ps1
```

The editor binary is `build/bin/<Configuration>/SatEngine.exe`.

### Standalone game (no editor UI)

```powershell
.\scripts\build.ps1 -Target GamePlayer -Configuration Release -ProjectFolder .\Examples\LuaCube
.\scripts\run.ps1 -Target GamePlayer -Configuration Release
```

Or with CMake presets:

```powershell
cmake --preset windows-gameplayer -DSAT_GAME_PROJECT="${PWD}/Examples/LuaCube"
cmake --build --preset windows-gameplayer-release
.\build-gameplayer\bin\Release\LuaCube.exe
```

From the editor, **Build > Build Standalone** writes
`<project>/Build/<project-folder-name>.exe` plus its `.pak` files.

<details>
<summary>More build options</summary>

- `SAT_BUILD_EDITOR=OFF` skips ImGui and the editor target.
- Default `windows` preset can build either `--target Editor` or `--target GamePlayer`.
- Game-player helper output: `<project>/Build/<name>.exe` (intermediates in `<project>/Build/.satengine-build/`).
- Install outside the repo:

```powershell
cmake --install build-gameplayer --config Release --prefix dist/SatGame
.\dist\SatGame\LuaCube.exe
```

Ship the exe together with its `.pak` parts. An MSVC runtime is required on the target machine.

</details>

## Docs

| Topic | Guide |
| --- | --- |
| Lua scripting | [docs/LuaScripting.md](docs/LuaScripting.md) |
| Meshes, materials & textures | [docs/AssetLoading.md](docs/AssetLoading.md) |
| Lighting | [docs/Lighting.md](docs/Lighting.md) |
| Shaders | [docs/Shaders.md](docs/Shaders.md) |
| Move / rotate / scale | [docs/SceneTransforms.md](docs/SceneTransforms.md) |
| Physics | [docs/Physics.md](docs/Physics.md) |
| Audio | [docs/Audio.md](docs/Audio.md) |

## Project layout

| Path / target | Role |
| --- | --- |
| `Editor/` | Editor host → `SatEngine.exe` |
| `GamePlayer/` | Standalone player → `<project>.exe` |
| `Engine/` | Core, renderer, physics, audio, scripting, UI |
| `Examples/` | Sample projects |
| `docs/` | Feature guides |
| `scripts/` | Build and run helpers |

Library split (for contributors): `SatScene` → `SatScripting` (+ physics/audio) → `SatRuntime` → `SatEditor`.

## Tests

```powershell
# Windows (after a normal configure with BUILD_TESTING=ON)
ctest --test-dir build -C Debug --output-on-failure
```

Scene-only tests (no GLFW / D3D11 / ImGui) also build on macOS:

```sh
cmake -S . -B /tmp/sat-scene-tests -DSAT_BUILD_GAME_PLAYER=OFF -DSAT_BUILD_EDITOR=OFF -DBUILD_TESTING=ON
cmake --build /tmp/sat-scene-tests
ctest --test-dir /tmp/sat-scene-tests --output-on-failure
```

## License

**TBD** — not decided yet while the project is still in flux.
