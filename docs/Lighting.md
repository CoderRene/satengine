# Lighting

Open `Examples/LightingDemo` to see metallic and rough materials under one
directional light, two point lights, and a spot light. Open **Game** to view
the scene from the main camera.

Add a light from the **Hierarchy** context menu or from **Inspector > Add
Component**. The light's direction follows the entity's local +Z axis in
SatEngine's left-handed coordinate system. Position, range, and cone angles
are authored in world units and degrees. Cone angles are measured from the
center axis. The Scene view shows debug lines for the selected light.

- Directional lights illuminate the scene from a fixed direction.
- Point lights radiate in all directions from their position, with smooth
  inverse-square falloff that reaches zero at the configured range.
- Spot lights add a cone to the point-light falloff. The light fades between
  the inner and outer angles.

Adjust ambient color and intensity in **Scene > Lighting Settings**. Ambient
contributes to the diffuse response. Each view uses at most 128 lights.
If the scene exceeds the limit, directional lights take priority, followed
by the closest point and spot lights to that view's camera.

The mesh shader combines GGX specular, Lambert diffuse, ambient, and emissive
contributions. It writes linear light to an FP16 target. The post-processing pass adds bloom
from values above 1, then applies Reinhard tone mapping and sRGB encoding.
Editor overlays draw after post-processing. See [Asset Loading](AssetLoading.md) for material authoring.

## Lua

```lua
local light = sat.createEntity("Key Light")
sat.addLight(light)
sat.setLightType(light, "spot")
sat.setPosition(light, 0, 3, -4)
sat.setRotation(light, 30, 0, 0)
sat.setLightColor(light, 1, 0.8, 0.6)
sat.setLightIntensity(light, 20)
sat.setLightRange(light, 12)
sat.setLightCone(light, 15, 30)
sat.setAmbient(0.8, 0.9, 1, 0.2)
```

Use `hasLight` and `removeLight` to inspect or remove the component. The
lighting setters have corresponding getters. `getAmbient` returns RGB
followed by intensity.

## Shadows, environment, and opacity

- **Directional Shadows** uses up to four camera-frustum cascades for the first
  active directional light. Under **Scene → Lighting Settings**, set
  **Cascades** (1–4), **Shadow Distance** (10–1000 world units),
  **Split Distribution** (0 for even splits, 1 for logarithmic splits), and
  **Cascade Resolution** (512, 1024, 2048, or 4096 pixels per cascade).
  The defaults are four cascades, 100 world units, a split distribution of 0.75,
  and 2048 pixels per cascade. Opaque and masked geometry cast shadows. The
  default depth array uses 64 MiB of GPU memory.
- **Environment Map** accepts an equirectangular PNG or JPEG under `Assets/`.
  Enable mipmap generation on the texture to give rough reflections a blurrier
  level. The shader approximates diffuse and specular image-based lighting by
  sampling the texture's mip chain. The environment lights the materials while
  the camera clear color supplies the background.
- **Alpha Mode** in the material Inspector controls opacity using base-color
  alpha times texture alpha. **Mask** discards fragments below **Alpha Cutoff**.
  **Blend** draws after opaque and masked geometry, sorted by object depth with
  depth testing enabled and depth writes disabled. Intersecting transparent
  objects and overlapping triangles within a mesh can show sorting artifacts.

## Bloom

Open **Scene → Lighting Settings → Bloom**. These settings are saved per scene
and used by both viewports and GamePlayer.

| Control | Effect |
| --- | --- |
| Enable Bloom | Turns the glow on or off |
| Threshold | Selects highlights by peak linear HDR channel value |
| Soft Knee | Widens the transition around the threshold as a fraction of the threshold |
| Intensity | Scales the glow added before tone mapping |
| Radius | Controls the coarsest scale blended into the glow |
| Tint | Colors the glow without changing the source surface |
| Highlight Clamp | Limits the brightest contribution to the glow |
| Sampling Quality | Selects 9, 25, or 49 taps per filter pass. Higher settings cost more GPU time |
| Reset Bloom Defaults | Restores the original bloom look |
