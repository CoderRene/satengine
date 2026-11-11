# Mesh and texture assets

Place project files under `Assets/`, then select an entity with a Mesh component.
In **Inspector > Mesh**, choose **Geometry** (a static `.obj`, or Built-in Cube)
and **Texture** (a `.png`, `.jpg`, or `.jpeg`, or None). Dropdowns rescan when
opened. Assets can be used by multiple entities; their decoded data and GPU
resources are shared within the renderer.

Open `Examples/AssetLoading` for a spinning textured pyramid and textured cube.
Save the scene before exporting. The same mesh and texture references are used
in the editor's Scene/Game views and the standalone player.

## Lua

```lua
function onStart()
    local object = sat.createEntity("Imported model")
    sat.addMesh(object)
    sat.setMeshAsset(object, "Assets/Meshes/Pyramid.obj")
    sat.setTextureAsset(object, "Assets/Textures/Checker.png")
end
```

- `sat.getMeshAsset(id)` / `sat.getTextureAsset(id)` return the assigned path,
  or nil for built-in geometry / no texture.
- `sat.setMeshAsset(id, path)` assigns static OBJ geometry. Nil or an empty
  string restores the built-in cube.
- `sat.setTextureAsset(id, path)` assigns the mesh's color texture. Nil or an
  empty string restores plain color.

These functions require a Mesh component and a live stable entity ID. Setters
validate path syntax and supported extension before changing the component;
actual file loading occurs on first use by the renderer. Missing or malformed
files appear in **Asset Errors** (or `SatGame.log` in a built game). Missing
geometry is skipped; missing textures render plain color. Filename extensions
are case-insensitive; the path's spelling/case must match the packaged entry.

## Supported formats and conventions

This first asset path renders opaque, unlit static geometry with one color
texture per entity. Mesh instances keep the existing color tint. Texture colors
are preserved through the existing unlit UNORM pipeline; alpha transparency,
lighting, materials, animation, skeletal meshes, FBX and glTF are not implemented.

OBJ support includes positions (`v`), UVs (`vt`), normals (`vn`, validated but not
used for lighting), `v`, `v/vt`, `v//vn` and `v/vt/vn` face references, positive
and negative indices, triangles and convex polygons (fan-triangulated). Export
triangulated meshes for general geometry. Objects/groups are combined into one
mesh. Material-library/material-name records are ignored; assign the texture
explicitly in Inspector or Lua. Vertex colors and free-form surfaces are not
supported. Optional position W must be 1; optional UV W must be 0.

By default, OBJ coordinates are treated as right-handed with +Y up. Import
reflects Z and reverses face winding to match the engine's left-handed +Z-forward
convention, and flips UV V for top-row-first images. Import settings can instead
preserve left-handed source coordinates, disable the V flip, and apply a uniform
scale. There is no automatic recentering or unit detection.
Imported meshes retain local bounds for editor selection; outlines use the
actual imported geometry. Picking uses the transformed bounding box.

PNG/JPEG textures decode to top-row-first RGBA8. Bilinear filtering, repeat
addressing, and a single mip level remain the defaults. Import settings also
support resizing, mip generation, point/trilinear filtering, and clamp addressing.
The built-in cube now has per-face UVs and can also use textures.

## Import settings in Inspector

Single-click an OBJ, PNG or JPEG in either Content view to show its import
settings in Inspector. Selecting an entity in Hierarchy or Scene restores the
entity Inspector. Source files remain unchanged; settings apply to every entity
using the selected asset.

| Mesh setting | Default | Behavior |
| --- | --- | --- |
| Scale | 1 | Positive finite uniform scale; bounds are recomputed |
| Source coordinates | Right-handed | Reflect Z and reverse winding, or preserve a left-handed source |
| Flip UV V | Enabled | Convert bottom-origin UVs to top-row-first texture addressing |

| Texture setting | Default | Behavior |
| --- | --- | --- |
| Maximum size | 4096 | Powers of two from 32 to 4096; preserve aspect ratio, never upscale |
| Filter | Bilinear | Point, Bilinear, or Trilinear |
| Wrap | Repeat | Repeat or Clamp for both UV axes |
| Generate mipmaps | Disabled | Generate all lower levels through 1x1 |

**Apply** stages CPU data and GPU resources, atomically saves the sidecar, then
replaces only that asset. A failed import or save preserves the previous working
asset and saved settings. **Revert** discards edits and reads saved settings.
**Reimport** retries the source using saved settings; it is disabled while edits
are unapplied. Imported dimensions/mip counts or vertex/triangle counts appear
below the controls.

Selection changes, project changes, entering Play, and closing the editor prompt
with **Apply / Revert / Cancel** when edits are pending. Cancel keeps the draft
and current selection. Failed Apply leaves the prompt open with the error.
Import editing and reimporting are disabled during Play.

Settings are versioned text sidecars named `Model.obj.satimport` or
`Color.png.satimport`. They are hidden in Content and should be committed with
the source asset. No sidecar means legacy defaults; an empty, unreadable or
unsupported sidecar is an error. The file contains `SatImport 1`, followed by
`scale rightHanded flipV`, then `maximumSize filter wrap mipmaps`. Filter values
are Point=0, Bilinear=1, Trilinear=2; wrap values are Repeat=0, Clamp=1.

Resizing and mip generation use a portable area-weighted box filter, including
odd-sized edges. Filtering operates on the existing encoded UNORM color values;
this release does not add sRGB conversion or alpha-aware material rendering.
The example checker texture includes saved settings demonstrating a 32-pixel
maximum, trilinear filtering, and a full mip chain.

## Loading, reload and limits

Loading is synchronous on first use. Repeated draws use cached CPU data and
immutable GPU resources; repeated failures do not retry or log every frame.
Use the selected asset's **Reimport** after editing its source or fixing a failure.
Use **File > Reload Assets** to clear all cached assets and diagnostics. Opening another project clears both CPU and GPU caches.
There is no automatic file watcher or background streaming yet.

Asset paths are UTF-8, relative to the project root and begin with `Assets/`.
Absolute paths, traversal, empty segments, backslashes and control characters
are rejected. File sources also reject symlinks that resolve outside the project.
Files are bounded to 64 MiB; images to 4096×4096; imported meshes to one million
positions/UVs/normals/output vertices, three million triangle indices, and 256
corners per polygon. Decoded CPU data, including generated mip levels, is bounded to 256 MiB and
4096 unique paths. Sidecars are bounded to 4096 bytes.
Cache-limit failures are explicit; reload to reclaim cached resources.

Scene saves now write `SatScene 2`, including quoted mesh/texture paths. Existing
`SatScene 1` files still load with built-in geometry and no texture. Older engine
versions cannot read v2 scenes.

Standalone builds validate all supported mesh/texture assets and their sidecars
before creating archives, including assets referenced only by Lua. Orphaned or
unsupported sidecars are rejected, and sidecar changes trigger repacking.
The existing encrypted archive groups include OBJ files in the assets group and
images in the textures group. The player reads their bytes directly from the
loaded archives, without extracting files or falling back to project folders.
The builder includes supported assets and sidecars under `Assets/`; the player
uses the same importer and settings as the editor.

## Native integration

The portable `SatAssets` target supplies `decodeObj`, `decodeTexture`, and the
window-thread-only `AssetLibrary`. Decode failures preserve the destination.
`fileAssetReader(projectRoot)` supplies a bounded project-file source.
`AssetLibrary` reads sidecars on cache misses and caches successful and failed requests; returned data pointers
survive cache growth and expire when its reader changes, it is cleared, or it
is destroyed. Reader callbacks copy bytes out and must retain their captured
sources for the duration of use.

`Renderer::setAssetReader(reader)` resets project CPU/GPU caches. Runtime host
subclasses configure the reader during `Application::onStart()` using the
protected `setAssetReader` helper. All uploads, draws and GPU resource release
happen on the renderer thread. The renderer owns GPU resources through shutdown;
scene components only own paths.

Portable loading, serialization, Lua and archive tests run without D3D11. On
Windows, `AssetRenderingTests` additionally uses WARP to compile the mesh shaders,
upload imported geometry/mip chains, verify samplers and failed-save rollback,
draw offscreen and check pixel readback.
