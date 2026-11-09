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

## Build

```powershell
.\scripts\build.ps1
```

Or with CMake presets directly:

```powershell
cmake --preset windows
cmake --build --preset windows-debug
```

The Debug binary lands under `build/bin/Debug/`.

To run after a build:

```powershell
.\scripts\run.ps1
```

## Layout

| Path | Role |
| --- | --- |
| `Engine/` | Static library (`SatEngine`). Core, Platform, Renderer, Assets, UI |
| `Game/` | Thin executable that boots the engine |
| `scripts/` | Build / run helpers |
| `build/` | Generated output (not committed) |

## License

**TBD**, not decided yet while the project is still in flux.
