# Renderer shaders

Shader sources live in `Engine/Renderer/shaders`. Each pass provides
`vsMain` and `psMain`. The mesh pass imports Common for display encoding,
Brdf for GGX shading, Lighting for direct light evaluation, and Material
for texture sampling.

CMake downloads a pinned Windows x64 Slang release and uses `slangc` to
generate Shader Model 5.0 DXBC. `SatShaders` builds the bytecode and writes a
manifest containing the archive version, the SHA-256 of `ShaderLayout.hpp`,
and the expected filenames. The runtime checks the manifest against its
generated layout hash before creating the D3D11 programs.

The editor stages loose files under `Shaders/` next to `SatEngine.exe`.
The editor also stages a separate ImGui shader pair under `EditorShaders/Shaders/` for its DX11 backend.
GamePlayer reads the renderer entries from the encrypted `.assets.pak` group.
Rebuild the executable and archives together when changing a shader layout.

## Development reload

`SAT_SHADER_HOT_RELOAD` defaults to the value of `SAT_BUILD_EDITOR`.
With the option on, development builds stage the Slang compiler DLL.
The renderer starts from precompiled bytecode and polls source timestamps
twice per second. Saving a shader compiles the affected program. Both stages
and the input layout must succeed before the renderer swaps programs.
Compiler diagnostics go to stderr, and the previous program remains active
on failure. Release builds use precompiled bytecode only.

## Adding a pass

1. Add its Slang source and explicit resource register bindings.
2. Add its name to `cmake/SatShaders.cmake` and `kShaderNames` in the same
   order. Add its enum value in `ShaderId`.
3. Describe its constant buffers and vertex inputs in `ShaderLayout.hpp`,
   then register its input layout in `ShaderLibrary::replace`.
4. Fetch the program from the library on each draw. Keep the pass's buffers
   and render states owned by the pass.

## Windows verification

Run these commands from the repository root in Developer PowerShell for Visual Studio.

```powershell
cmake --preset windows -DBUILD_TESTING=ON -DSAT_SHADER_HOT_RELOAD=ON
cmake --build --preset windows-debug
ctest --test-dir build -C Debug --output-on-failure
& .\build\bin\Debug\SatEngine.exe
```

Open `Examples/LightingDemo`. Compare the Scene and Game views, edit the lights
and materials, and save and reopen the scene. Duplicate point lights until
there are more than 128 total. Directional lights should remain active and the
nearest point or spot lights should win the remaining slots. Open a pre-v6
scene to check loading with default ambient settings.

While the Debug editor is running, change the diffuse term in `Brdf.slang`.
The view should update after saving. Introduce a syntax error, check stderr,
and confirm that the last valid shader stays active. Restore the source before
the next build. Rename `build/bin/Debug/Shaders`, restart the editor, and
check that the Scene and Game views show the camera clear color and stderr
reports a missing shader. Restore the folder after the check.

```powershell
cmake --preset windows-gameplayer -DSAT_GAME_PROJECT:PATH="$PWD/Examples/LightingDemo"
cmake --build --preset windows-gameplayer-release
$player = ".\build-gameplayer\bin\Release\LightingDemo.exe"
dumpbin /dependents $player
Get-ChildItem .\build-gameplayer\bin\Release\*slang*.dll
python -c "import pathlib,sys; assert b'DXBC' not in pathlib.Path(sys.argv[1]).read_bytes()" $player
& $player
```

The Release player should render with its staged PAK files present. Its
dependencies should contain neither Slang nor D3DCompiler. To exercise the
layout guard with the same archive keys, back up the assets archive and its
parts, change `ShaderLayout.hpp`, and rebuild in the same build directory
with the same release ID. Restore the previous assets archive set alongside
the newly built executable. The player should report `Shader archive does not
match this build` and exit.

## Project material shaders

Place a .slang file under the project's Assets folder and select it in the
material Inspector's **Shader** dropdown. Save the material and assign it to
a mesh. The Stripes shader and material in LightingDemo provide a starting point.

Project shaders provide a fragment entry point named psMain with the
[shader("fragment")] attribute. SatEngine supplies the mesh vertex stage.
Import MeshFragment for the shared pixel input. It exposes SV_Position and
TEXCOORD0 through TEXCOORD4 for
render-origin-relative position, world normal, UV, world tangent with
handedness, and clip-space W respectively. Return linear HDR RGBA as SV_Target.
The engine applies tone mapping and bloom afterward. Import Material and use
sampleBaseColor and materialOpacity for the material's tint, texture, and
alpha mode. The engine supplies render states and sorts Blend materials.
Masked shadows use the standard base alpha. Custom shaders compute their
own shading.

Each project .slang asset provides its own fragment entry. Shared engine
modules are available through Slang imports. A Debug editor with
SAT_SHADER_HOT_RELOAD enabled compiles a shader when a material first uses it,
then checks for edits. Compiler diagnostics go to stderr. The last working
shader stays active, with Lit (PBR) as the fallback before the first success.
Editor configurations without the runtime compiler use the fallback.

GamePlayer builds compile project shaders and package their bytecode under
Shaders/Project in the assets PAK. A material that references a missing
project shader fails packaging.
