#!/usr/bin/env python3
"""Generate deterministic SatEngine render projects and summarize benchmark CSVs."""

import argparse
import csv
import math
import random
import struct
import zlib
from pathlib import Path
import shutil
import statistics

ROOT = Path(__file__).resolve().parent.parent
SIZES = (10_000, 50_000, 100_000)
# Disabled components in the SatScene 17 entity record.
NO_MESH = '0 0 "" "" 0 0'
NO_CAMERA = "0 0 60 0.1 100 0 0 0 1"
NO_ANIMATOR = '0 "" "" 0 0 1 0'
NO_LIGHT = "0 0 1 1 1 1 10 20 35"
NO_PHYSICS_AUDIO = (
    "0 1 1 1 0 0 1 0 63 0 0 0 0 0 0",
    "0 0 0.5 0 0",
    "0 0 9.81 1 0 -9.81 0 0",
    "0 0",
)


def entity(name, transform, mesh=NO_MESH, camera=NO_CAMERA, animator=NO_ANIMATOR, light=NO_LIGHT, scripts=()):
    return "\n".join((
        f'"{name}"', transform, mesh, camera, str(len(scripts)), *scripts, *NO_PHYSICS_AUDIO,
        animator, light, "0",
    )) + "\n"


def write_obj(path, height):
    path.write_text(
        "v -.5 0 -.5\nv .5 0 -.5\nv .5 0 .5\nv -.5 0 .5\n"
        f"v 0 {height:.5f} 0\n"
        "f 1 2 3 4\nf 2 1 5\nf 3 2 5\nf 4 3 5\nf 1 4 5\n",
        encoding="utf-8",
    )


def generate(destination, count, away):
    name = f"RenderStress{count // 1000}k" + ("Away" if away else "")
    project = destination / name
    assets = project / "Assets"
    # Use a fresh project so reruns cannot overwrite user scene edits.
    project.mkdir(parents=True)
    assets.mkdir()
    (project / "SatEngineProject.sat").write_text(f"name={name}\n", encoding="utf-8")
    example = ROOT / "Examples" / "GltfAnimation" / "Assets"
    for file in ("Beacon.gltf", "Beacon.gltf.satimport"):
        shutil.copyfile(example / file, assets / file)
    for index in range(8):
        write_obj(assets / f"Cell{index}.obj", 0.6 + index / 12)

    with (project / "Scene.sat").open("w", encoding="utf-8", newline="\n") as scene:
        scene.write(
            f"SatScene 17\n{count}\n0.016666666666666666 8 2000 1000 1000 65536 262144\n"
            '1 1 1 1 343 64 0 0 0 0\n1 1 1 0.4\n"" 1 1 1024 150 4 0.75\n'
            "0 1.2 0.47 0.8 64 1 1 1 65504 1\n"
        )
        # The offset exercises camera-relative rendering and shadow snapping.
        origin = 1_000_000_000
        scene.write(entity("Camera", f"{origin} 16 -35 0 {180 if away else 0} 0 1 1 1",
                           camera="1 1 60 0.1 2000 0.07 0.10 0.16 1"))
        scene.write(entity("Sun", "0 0 0 50 -25 0 1 1 1", light="1 0 1 0.95 0.85 3 10 20 35"))
        for index in range(6):
            scene.write(entity(f"Occluder{index}", f"{origin - 60 + index * 24} 9 28 0 0 0 14 18 2",
                               mesh='1 0 "" "" 0 0'))

        side = math.ceil(math.sqrt(count - 8))
        for index in range(count - 8):
            x = origin + (index % side - side // 2) * 3
            z = 35 + (index // side) * 3
            y = (index % 3) * 3
            variant = index % 256
            animator = NO_ANIMATOR
            if variant == 0:
                mesh = '1 0 "Assets/Beacon.gltf" "" 0 0'
                animator = '1 "Signal" "" 1 1 1 0'
            elif variant == 1:
                filename = f"Unique{index}.obj"
                write_obj(assets / filename, 0.5 + (index % 997) / 997)
                mesh = f'1 0 "Assets/{filename}" "" 0 0'
            elif 2 <= variant < 18:
                mesh = f'1 0 "Assets/Cell{variant % 8}.obj" "" 1 0'
            else:
                mesh = '1 0 "" "" 0 0'
            scene.write(entity(f"Mesh{index}", f"{x} {y} {z} 0 {index % 360} 0 1 1 1",
                               mesh=mesh, animator=animator))
        scene.write("0\n" * count)  # All entities at scene root.
    print(project.resolve())


def generate_animation(destination):
    name = "AnimationStress200"
    project = destination / name
    project.mkdir(parents=True)
    assets = project / "Assets"
    assets.mkdir()
    example = ROOT / "Examples" / "GltfAnimation"
    for file in ("Beacon.gltf", "Beacon.gltf.satimport"):
        shutil.copyfile(example / "Assets" / file, assets / file)
    (project / "SatEngineProject.sat").write_text(f"name={name}\n", encoding="utf-8")
    # Use the shipped v17 scene settings for identical pre- and post-change captures.
    header = (example / "Scene.sat").read_text(encoding="utf-8").splitlines()[:7]
    header[1] = "202"
    with (project / "Scene.sat").open("w", encoding="utf-8", newline="\n") as scene:
        scene.write("\n".join(header) + "\n")
        scene.write(entity("Camera", "0 8 -4 -5 0 0 1 1 1",
                           camera="1 1 65 0.1 150 0.07 0.10 0.16 1"))
        scene.write(entity("Sun", "0 0 0 50 -25 0 1 1 1", light="1 0 1 0.95 0.85 3 10 20 35"))
        for group, start in (("Near", 6), ("Middle", 30), ("Far", 64), ("Offscreen", -35)):
            for index in range(50):
                x = (index % 10 - 4.5) * 1.6
                z = start + index // 10 * 1.5
                scene.write(entity(f"{group} Beacon {index + 1}", f"{x:g} 0 {z:g} 0 {index * 13 % 360} 0 1 1 1",
                                   mesh='1 0 "Assets/Beacon.gltf" "" 0 0', animator='1 "Signal" "" 1 1 1 0'))
        scene.write("0\n" * 202)
    print(project.resolve())


def write_png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    stride = width * 3
    rows = b"".join(b"\0" + pixels[start:start + stride] for start in range(0, len(pixels), stride))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, level=1))
        + chunk(b"IEND", b"")
    )


def generate_hangar(destination, unused_textures, baseline):
    name = "HangarStress"
    project = destination / name
    project.mkdir(parents=True)
    assets = project / "Assets"
    ship = assets / "Meshes" / "Ship"
    ship.mkdir(parents=True)
    scripts = project / "Scripts"
    scripts.mkdir()
    (project / "SatEngineProject.sat").write_text(f"name={name}\n", encoding="utf-8")
    source = ROOT / "Examples" / "AssetLoading" / "Assets" / "Meshes" / "Ship"
    for filename in ("Ship.gltf", "Ship.bin", "ShipPaint.png", "ShipPaint.png.satimport", "Ship.satmat"):
        shutil.copyfile(source / filename, ship / filename)
    write_obj(assets / "LuaOnly.obj", 2.5)
    (project / "StandaloneIncludes.txt").write_text("Assets/LuaOnly.obj\n", encoding="utf-8")
    (scripts / "hangar.lua").write_text(
        """local frame = 0
local time = 0
function onUpdate(deltaSeconds)
    frame = frame + 1
    time = time + deltaSeconds
    sat.setPosition(self.entity, math.sin(time) * 2.5, 2, 13)
    if frame == 30 then
        local mesh = sat.createEntity("Lua-only beacon")
        sat.addMesh(mesh)
        sat.setPosition(mesh, 1, 1, 8)
        sat.setMeshAsset(mesh, "Assets/" .. "LuaOnly.obj")
    end
end
""",
        encoding="utf-8",
    )
    random_source = random.Random(741)
    stars = bytearray((9, 17, 37) * (512 * 256))
    for _ in range(500):
        index = random_source.randrange(512 * 256) * 3
        stars[index:index + 3] = bytes((180, 205, 255))
    # An asymmetric patch makes sky orientation and translation easy to compare.
    for y in range(101, 115):
        for x in range(280, 287):
            index = (y * 512 + x) * 3
            stars[index:index + 3] = bytes((245, 174, 96))
    write_png(assets / "Stars.png", 512, 256, stars)
    unused = assets / "Unused"
    unused.mkdir()
    for index in range(unused_textures):
        write_png(unused / f"Noise{index:03}.png", 512, 512, random_source.randbytes(512 * 512 * 3))
    for filename, color in (("Amber", "1 .6 .2 .45"), ("Blue", ".15 .45 1 .45")):
        (assets / f"{filename}.satmat").write_text(
            f'SatMaterial 5\nlit\nblend 0.5\n1\n{color}\n0 0.1\n0 0 0 1\n""\n""\n""\n""\n',
            encoding="utf-8",
        )
    records = []

    def add(name, transform, mesh=NO_MESH, camera=NO_CAMERA, light=NO_LIGHT, cast_shadow=False, scripts=()):
        shadow = "" if baseline else f" {int(cast_shadow)}"
        records.append(entity(name, transform, mesh, camera, NO_ANIMATOR + " 0 1 0", light + shadow, scripts))

    add("Camera", "0 3 -9 0 0 0 1 1 1", camera="1 1 60 0.1 150 .03 .04 .07 1")
    add("Hangar floor", "0 -0.1 18 0 0 0 28 .2 60", mesh='1 0 "" "" 0 0')
    add("Hangar door", "0 2 13 0 0 0 3 4 .2", mesh='1 0 "" "" 0 0', scripts=('1 "hangar.lua"\n0',))
    add("Door spot", "0 9 6 40 0 0 1 1 1", light="1 2 1 .9 .8 220 32 16 37", cast_shadow=True)
    add("Sun", "0 0 0 50 -25 0 1 1 1", light="1 0 .8 .9 1 .4 10 20 35")
    add("Amber glass", "-2 2 3 0 -27 0 3 3 .04", mesh='1 0 "" "Assets/Amber.satmat" 0 0')
    add("Blue glass", "-2 2 3 0 22 0 3 3 .04", mesh='1 0 "" "Assets/Blue.satmat" 0 0')
    for index in range(60):
        x = ((index % 10) - 4.5) * 2.5
        z = 16 + (index // 10) * 4
        material = "Assets/Meshes/Ship/Ship.satmat" if index % 2 == 0 else ""
        add(f"Ship {index:02}", f"{x:g} .8 {z} 0 {index * 17 % 360} 0 .7 .7 .7",
            mesh=f'1 0 "Assets/Meshes/Ship/Ship.gltf" "{material}" 0 0')
    header = (
        f"SatScene {19 if baseline else 20}\n{len(records)}\n"
        "0.016666666666666666 8 2000 1000 1000 65536 262144\n"
        '1 1 1 1 343 64 0 0 0 0\n1 1 1 .15\n"Assets/Stars.png" 1 1 1024 100 4 .75\n'
        "0 1.2 0.47 0.8 64 1 1 1 65504 1\n"
    )
    if not baseline:
        header += "1 1\n"
    (project / "Scene.sat").write_text(header + "".join(records) + "0\n" * len(records), encoding="utf-8")
    print(project.resolve())


def summarize(captures):
    for path in captures:
        with path.open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        print(path)
        for metric in ("frame_ms", "gpu_frame_ms", "cpu_animation_ms", "cpu_scene_sync_ms"):
            samples = sorted(float(row[metric]) for row in rows if row.get(metric))
            if not samples:
                continue
            worst = samples[-math.ceil(len(samples) * 0.01):]
            percentile = samples[math.ceil(len(samples) * 0.99) - 1]
            print(f"  {metric:<13} median {statistics.median(samples):8.3f}  "
                  f"p99 {percentile:8.3f}  worst 1% mean {statistics.mean(worst):8.3f}  n={len(samples)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generation = commands.add_parser("generate", help="Write stress projects")
    generation.add_argument("destination", type=Path)
    generation.add_argument("--count", type=int, choices=SIZES)
    generation.add_argument("--away", action="store_true", help="Point the camera away from the meshes")
    animation = commands.add_parser("animation", help="Write the 200-rig animation project")
    animation.add_argument("destination", type=Path)
    hangar = commands.add_parser("hangar", help="Write the asset-loading and visuals fixture")
    hangar.add_argument("destination", type=Path)
    hangar.add_argument("--unused-textures", type=int, default=24)
    hangar.add_argument("--baseline", action="store_true", help="Write a v19 scene compatible with the previous renderer")
    summary = commands.add_parser("summarize", help="Report median, p99 and worst 1%%")
    summary.add_argument("captures", type=Path, nargs="+")
    args = parser.parse_args()
    if args.command == "generate":
        for count in (args.count,) if args.count else SIZES:
            generate(args.destination, count, args.away)
    elif args.command == "animation":
        generate_animation(args.destination)
    elif args.command == "hangar":
        if not 0 <= args.unused_textures <= 4096:
            parser.error("--unused-textures must be between 0 and 4096")
        generate_hangar(args.destination, args.unused_textures, args.baseline)
    else:
        summarize(args.captures)


if __name__ == "__main__":
    main()
