# Mesh, material, and texture assets

Place project files under `Assets/`, then select an entity with a Mesh component.
In **Inspector > Mesh**, choose **Geometry** (a static `.obj`, or Built-in Cube)
and **Material** (a `.satmat` or the built-in default). Dropdowns rescan when
opened. Assets can be used by multiple entities; their decoded data and GPU
resources are shared within the renderer.

Open `Examples/AssetLoading` for a spinning textured pyramid and textured cube.
Save the scene before exporting. The same mesh and material references are used
in the editor's Scene/Game views and the standalone player.

## Lua

```lua
function onStart()
    local object = sat.createEntity("Imported model")
    sat.addMesh(object)
    sat.setMeshAsset(object, "Assets/Meshes/Pyramid.obj")
    sat.setMaterialAsset(object, "Assets/Materials/Checker.satmat")
end
```

- `sat.getMeshAsset(id)` / `sat.getMaterialAsset(id)` return the assigned path,
  or nil for built-in geometry or the default material.
- `sat.setMeshAsset(id, path)` assigns static OBJ geometry. Nil or an empty
  string restores the built-in cube.
- `sat.setMaterialAsset(id, path)` assigns a shared material. Nil or an
  empty string restores the default material.

These functions require a Mesh component and a live stable entity ID. Setters
validate path syntax and supported extension before changing the component;
actual file loading occurs on first use by the renderer. Missing or malformed
files appear in **Asset Errors** (or `SatGame.log` in a built game). Missing
geometry is skipped. Missing materials and textures use defaults. Filename extensions
are case-insensitive; the path's spelling/case must match the packaged entry.

## Supported formats and conventions

Opaque static meshes use forward metallic-roughness shading. See [Lighting](Lighting.md) for scene controls.

OBJ support includes positions (`v`), UVs (`vt`), normals (`vn`, with flat face normals generated when omitted), `v`, `v/vt`, `v//vn` and `v/vt/vn` face references, positive
and negative indices, triangles and convex polygons (fan-triangulated). Export
triangulated meshes for general geometry. Objects/groups are combined into one
mesh. Material-library/material-name records are ignored; assign the material
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

## Material assets

Right-click in **Content** and choose **Create Material**. Select the resulting
`.satmat` to edit its parameters and assign PNG or JPEG maps. **Save Material**
updates every mesh using that path. Assign it in the entity's Mesh inspector.
In Content's icon view, material thumbnails render a cube under fixed preview
lighting. Saving a material, reimporting a texture, or hot-reloading a shader
refreshes the cached previews.

Choose **Lit (PBR)**, **Unlit**, or a project Slang shader in the material's **Shader** dropdown. Unlit
displays the base color and its texture independently of scene lighting.

Set **Specular Strength** to 0 to remove direct highlights and environment
reflections while retaining diffuse lighting and shadows. Metallic still controls
how much diffuse light the material scatters.

`SatMaterial 5` stores the shader on the line after the header. Use `lit`, `unlit`,
or `custom "Assets/Shaders/Example.slang"`. The next line stores an
alpha mode (`opaque`, `mask`, or `blend`) and cutoff, then specular strength on
its own line. Versions 1–3 default to specular strength 1. Version 1 materials
load as Lit (PBR), and versions 1 and 2 load as Opaque. All versions store base color RGBA,
metallic and roughness, emissive RGB and
normal scale, then four quoted paths in this order: base color, metallic-roughness,
normal, and emissive. Paths are relative to the project root under `Assets/`.
Empty paths select defaults. Numeric values are finite and clamped to [0, 1].
The default material is white with metallic 0 and roughness 0.5.

Base color and emissive maps are sampled as sRGB. Normal and metallic-roughness
maps are linear, with roughness in green and metallic in blue. A material uses
the sampler settings of its first available texture in the order above. The
normal map uses tangent space, and OBJ tangents are generated during import.
Emissive RGB is the emission factor, multiplied by the emissive map when present.

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
odd-sized edges. Filtering during import averages encoded RGBA8 values. Material
sampling applies sRGB decoding for base color and emissive maps.
The example checker texture includes saved settings demonstrating a 32-pixel
maximum, bilinear filtering, and a full mip chain.

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
4096 unique paths. Sidecars are bounded to 4096 bytes and material files to 8192 bytes.
Cache-limit failures are explicit; reload to reclaim cached resources.

Scene saves write `SatScene 9` with quoted mesh and material paths. Versions 1–8
still load. Versions 1–5 use default ambient settings. Legacy texture references are discarded
with a load warning. Create a material that references the texture to restore it.

Standalone builds validate supported meshes, materials, textures, and their sidecars
before creating archives, including assets referenced only by Lua. Orphaned or
unsupported sidecars are rejected, and sidecar changes trigger repacking.
The existing encrypted archive groups include OBJ and SATMAT files in the assets group and
images in the textures group. The player reads their bytes directly from the
loaded archives, without extracting files or falling back to project folders.
The builder includes supported assets and sidecars under `Assets/`; the player
uses the same importer and settings as the editor.

## Native integration

The portable `SatAssets` target supplies `decodeObj`, `decodeTexture`, `decodeMaterial`, and the
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
Windows, `AssetRenderingTests` additionally uses WARP to load the compiled mesh shaders,
upload imported geometry/mip chains, verify samplers and failed-save rollback,
draw offscreen and check pixel readback.
