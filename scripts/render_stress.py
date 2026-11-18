#!/usr/bin/env python3
"""Generate deterministic SatEngine render projects and summarize benchmark CSVs."""

import argparse
import csv
import math
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


def entity(name, transform, mesh=NO_MESH, camera=NO_CAMERA, animator=NO_ANIMATOR, light=NO_LIGHT):
    return "\n".join((
        f'"{name}"', transform, mesh, camera, "0", *NO_PHYSICS_AUDIO,
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


def summarize(captures):
    for path in captures:
        with path.open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        print(path)
        for metric in ("frame_ms", "gpu_frame_ms"):
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
    summary = commands.add_parser("summarize", help="Report median, p99 and worst 1%%")
    summary.add_argument("captures", type=Path, nargs="+")
    args = parser.parse_args()
    if args.command == "generate":
        for count in (args.count,) if args.count else SIZES:
            generate(args.destination, count, args.away)
    else:
        summarize(args.captures)


if __name__ == "__main__":
    main()
