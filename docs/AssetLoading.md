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
Empty paths select defaults. Version 6 adds the occlusion strength and texture.
Version 7 stores double-sided rendering and per-map UV and sampler settings.
Version 8 adds transmission, index of refraction, thickness, attenuation, and
the transmission and thickness texture slots. Version 9 adds clearcoat and its
weight, roughness, and normal maps.
Numeric factors are finite. Starting with version 7, normal scale is signed and alpha
cutoff may exceed 1. The default material is white with metallic 0 and roughness 0.5.

Base color and emissive maps are sampled as sRGB. Normal, metallic-roughness and
occlusion maps are linear, with roughness in green, metallic in blue, and
occlusion in red. The normal map uses tangent space, and OBJ tangents are
generated during import. Emissive RGB is multiplied by the emissive map when present.

Choose **glTF Materials** in the Mesh inspector to use the model's authored
materials. Each supported glTF texture slot can use `TEXCOORD_0` or
`TEXCOORD_1`, a `KHR_texture_transform` transform, and its own wrap and filter
settings. When no sampler is specified, the texture's import settings apply.

glTF opacity combines the base color factor, texture alpha, and vertex alpha.
`OPAQUE` ignores the result, `MASK` uses the authored cutoff, and `BLEND` draws
sections back to front. Double-sided materials render from both sides.
Imported glass uses `KHR_materials_transmission`, `KHR_materials_ior`, and
`KHR_materials_volume`. Transmission uses the red texture channel and thickness
uses green. Roughness and index of refraction drive automatic transmission blur
up to mip 3. The manual offset can select coarser levels.
**Transmission Mip** ranges from 0 to 14 and offsets the calculated mip level by
the selected value minus 1. A value of 1 uses the authored roughness response,
0 sharpens by one mip level, and higher values increase blur. Fractional values
blend adjacent levels. The renderer clamps the result to the viewport’s mip
chain. **Roughness Blur Strength** scales the roughness contribution. At 0,
Transmission Mip alone sets the blur. At 1, the material uses its full
roughness response. SatMaterial version 10 stores Transmission Mip, and version 11
appends Roughness Blur Strength on the last line. Both settings default to 1. To tune an
imported material, extract it to a `.satmat` and assign it in the Mesh inspector.
Volume materials also bend and attenuate that sample based on thickness.
`KHR_materials_clearcoat` adds a separate reflection layer with its own
roughness and normal, so a polished surface can reflect sharply over a rough
or translucent base.
This screen-space pass captures opaque geometry and uses a depth test against it.
`OPAQUE` coverage is appropriate for a continuous glass surface. Use `MASK`
when the surface has cutouts. Extracted material assets preserve these settings.

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

The editor checks the open project’s `Assets/` folder for external changes.
Saving a mesh, texture, material, or import sidecar refreshes affected GPU
resources and cached previews. External glTF buffers and images are tracked
through the imports and materials that use them. A failed reload preserves the
previous GPU resource and reports the asset path in **Asset Errors**. Fix and
save the file again to retry. The editor defers automatic reloads during Play
and while Inspector import edits are pending.

First-use mesh and external texture decoding use up to three CPU workers.
Each worker slot holds one job until the render thread consumes its result. The
render thread can publish several ready assets per frame and stops after its
2 ms upload budget is spent. A single upload can exceed the budget. Geometry
appears as soon as its buffers are ready, while external textures continue
loading. Selecting or dragging a mesh in Content starts prefetching before
the drop, and queued geometry gets priority over texture jobs. Assigned
base-color maps use the pink fallback while unavailable.
Use **Reimport** for a manual retry or **File > Reload Assets** to reset the
project’s caches and diagnostics.

Asset paths are UTF-8, relative to the project root and begin with `Assets/`.
Absolute paths, traversal, empty segments, backslashes and control characters
are rejected. File sources also reject symlinks that resolve outside the project.
Files are bounded to 64 MiB; images to 4096×4096; imported meshes to one million
positions/UVs/normals/output vertices, three million triangle indices, and 256
corners per polygon. When the 256 MiB decoded mesh and texture cache fills, it
evicts the least recently used data to make room. Evicted assets are decoded again
when their CPU data is requested. Generated mip levels count toward this budget.
The asset caches also enforce a 4096-path limit. Sidecars are bounded to 4096 bytes
and material files to 8192 bytes.

Scene saves write `SatScene 10` with quoted mesh and material paths. Versions 1–9
still load. Versions 1–5 use default ambient settings. Legacy texture references are discarded
with a load warning. Create a material that references the texture to restore it.

Standalone builds validate the selected meshes, materials, textures, and
sidecars before creating archives. Orphaned or unsupported sidecars are
rejected, and sidecar changes trigger repacking.

The packer cooks eligible LDR textures into GPU-ready BC7 blocks, including
model-embedded images. Images that exceed the compression error limits, HDR
images, and images with dimensions unsuited to BC7 keep the source import
path. The original images are packaged for UI and compatibility. Large
external textures with mipmaps include a separate coarse mip chain. The
player can display that chain before loading the full texture.

In the editor, **Apply** and **Reimport** cache eligible textures under the
project's `.cache/sat-textures-v1` folder, including a small preview chain
for eligible large mipmapped images. Subsequent loads can show that preview
before loading full resolution while the source bytes and import settings
match. First appearances take priority over queued full-resolution refinements.
This folder can be deleted to clear the disk cache.
Standalone archives load their authenticated indexes at startup. The player
reads and authenticates asset chunks on demand and keeps the last chunk from
each group in an 8 MiB cache. It uses the same importer and settings as the
editor. Legacy version 2 archives still use the eager archive reader.

**Build → Build Standalone → Full Project** includes the complete supported
asset folder. **Referenced Assets** follows dependencies from the saved scene
and includes its compiled scripts. Add runtime-selected content to
`StandaloneIncludes.txt` next to `Scene.sat`, one exact project-relative file or
folder path under `Assets/` per line. Blank lines and lines starting with `#`
are accepted. Include any assets chosen in Lua, as well as stylesheets, images
and other files opened by UI documents at runtime. For example, a line
containing `Assets/Ships` includes the contents of that folder.

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
