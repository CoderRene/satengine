# GPU scene benchmark

Run these commands from the repository root in Windows PowerShell. The standalone build uses the repository's normal private packaging configuration.

## Build and test

```powershell
cmake --preset windows -DSAT_BUILD_GAME_PLAYER=ON -DBUILD_TESTING=ON
cmake --build --preset windows-debug
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\Debug\SatEngine.exe
```

The Windows tests include WARP coverage for frustum culling, dirty transform updates, shared instance streams, and the second opaque pass recovering a mesh when an occluder moves.

## Capture frame times

The generator writes projects with 10,000, 50,000, and 100,000 entities. Each project mixes shared cubes, distinct mesh assets, animated glTF meshes, and sources eligible for static cell batching. The `Away` variants point the camera away from that geometry.

```powershell
$stress = Join-Path $env:TEMP 'SatRenderStress'
python .\scripts\render_stress.py generate $stress
python .\scripts\render_stress.py generate $stress --away

foreach ($project in Get-ChildItem $stress -Directory -Filter 'RenderStress*') {
    $name = $project.Name
    cmake --preset windows-gameplayer "-DSAT_GAME_PROJECT=$($project.FullName)"
    cmake --build --preset windows-gameplayer-release
    $env:SAT_RENDER_BENCHMARK = Join-Path $stress "$name-gpu.csv"
    Start-Process ".\build-gameplayer\bin\Release\$name.exe" -Wait
}
Remove-Item Env:SAT_RENDER_BENCHMARK
$captures = (Get-ChildItem $stress -Filter '*.csv').FullName
python .\scripts\render_stress.py summarize $captures
```

The capture skips 120 warm-up frames, records 600 samples, and closes the player. Compare median, p99, and worst 1% mean at identical resolution and graphics settings. The GPU frame field uses the latest completed timestamp query and can lag the CPU frame field.

To collect a CPU-renderer baseline, use a checkout from before the renderer change and apply only the benchmark capture and larger scene-file allowance from this change. For an uncommitted implementation based on `HEAD`, prepare the baseline checkout with these commands.

```powershell
git worktree add --detach ..\sat-render-baseline HEAD
git diff --ignore-space-at-eol -- Engine/Core/src/Application.cpp Engine/Core/src/SceneFile.cpp |
    git -C ..\sat-render-baseline apply --ignore-space-change
```

Repeat the capture loop from that checkout with the same `$stress` and set the CSV suffix to `-baseline.csv`. The baseline capture uses the existing whole-frame GPU timestamps.

Scene sync compares cached transforms because gameplay can write them directly. Matrix reconstruction and steady-state GPU instance updates follow the changed instances. Topology changes and buffer growth upload the current scene. Oversized static cells remain on the instanced path.

## Visual checks

- Enable **Stats** in Game View. Compare GPU shadow, opaque, transmission, transparent, and post timings, and CPU scene sync and mesh submission timings. Geometry counts for indirect draws come from completed GPU readbacks.
- Check the shadow cascade boundaries during camera movement and inspect glass, blended layers, and masked surfaces in the asset examples.
- Play the animation example and check skinned meshes in both views, their selection outlines, and their thumbnails.
- Turn the scene camera quickly toward occluded geometry and move an occluder out of the way. Geometry visible in the new view should appear in that frame.
- Move a combined-static mesh within its cell, then across a boundary. Compare the frame times and check that dirty sources remain visible during rebuilding.
