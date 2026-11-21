# Hangar Demo

Open the `HangarDemo` folder as a project in SatEngine. Open **Game** for the
authored camera and press **Play**. The sliding door moves through the spot
light and casts a moving shadow on the deck. Lua also spawns a beacon near
the center walkway.

- Select **Door spot** in the hierarchy to toggle **Cast Shadow**.
- In **Scene → Lighting Settings**, toggle **Show Environment Sky** and
  **Weighted Blended Transparency**. Orbit the intersecting amber and blue
  panels in Scene view to compare weighted and sorted blending.
- With Play stopped, resave `Assets/Meshes/Ship/ShipPaint.png` using an image
  editor. The shared texture updates the ships and their material preview.
- Compare **Full Project** and **Referenced Assets** under
  **Build → Build Standalone**. The referenced export omits the spare paint
  texture. `StandaloneIncludes.txt` keeps the beacon mesh that Lua selects at
  runtime.
