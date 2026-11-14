# Game UI Demo

Open this folder as a project in SatEngine, press **Play**, and use the **Game**
view.

- Click **Add click** to increment the counter and **Reset** to clear it.
- Click **Pilot name** and type to update the greeting.
- Scroll inside the box at the bottom of the HUD.
- Stop and restart Play to reset the HUD.

Build and run the same project in GamePlayer from the repository root.

```powershell
.\scripts\build.ps1 -Target GamePlayer -Configuration Release -ProjectFolder .\Examples\GameUiDemo -SkipObfuscation
.\scripts\run.ps1 -Target GamePlayer -Configuration Release -ProjectFolder .\Examples\GameUiDemo
```

Select the **HUD** entity in the Inspector to see its **Game UI** component.
The Lua script creates the `hud` model, which supplies the counter and greeting.
The stripe uses a Slang shader, a gradient mask, and a drop shadow.

While playing in the editor, change the heading in `Assets/UI/hud.rml` and
save. The heading updates while the click count remains. Change a color in
`Assets/UI/hud.rcss` and save to see it update in the same Play session. See the
[Lua Game UI guide](../../docs/LuaScripting.md#game-ui).
