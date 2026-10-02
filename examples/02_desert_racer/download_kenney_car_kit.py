#!/usr/bin/env python3
"""Download and stage the CC0 Kenney Car Kit meshes used by this example.

The runtime still normalizes these OBJ files at load time in main.js so the
asset files remain close to the original Kenney distribution. This script does
only the reproducible asset-pipeline work needed by the demo:

- download the official Kenney Car Kit zip
- copy the original CC0 license
- extract every vehicle OBJ mesh from the kit
- print bounds / face counts for quick sanity checking

Source: https://kenney.nl/assets/car-kit
License: Creative Commons Zero (CC0)
"""

from __future__ import annotations

import argparse
import os
import sys
import urllib.request
import zipfile
from pathlib import Path

DOWNLOAD_URL = (
    "https://kenney.nl/media/pages/assets/car-kit/"
    "a9b1e99e92-1775131960/kenney_car-kit.zip"
)

VEHICLE_OBJS = [
    "ambulance.obj",
    "delivery-flat.obj",
    "delivery.obj",
    "firetruck.obj",
    "garbage-truck.obj",
    "hatchback-sports.obj",
    "kart-oobi.obj",
    "kart-oodi.obj",
    "kart-ooli.obj",
    "kart-oopi.obj",
    "kart-oozi.obj",
    "police.obj",
    "race-future.obj",
    "race.obj",
    "sedan-sports.obj",
    "sedan.obj",
    "suv-luxury.obj",
    "suv.obj",
    "taxi.obj",
    "tractor-police.obj",
    "tractor-shovel.obj",
    "tractor.obj",
    "truck-flat.obj",
    "truck.obj",
    "van.obj",
]


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {url}")
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    dest.write_bytes(data)
    print(f"Wrote {dest} ({len(data) / 1024:.1f} KiB)")


def obj_stats(text: str) -> tuple[int, int, int, int, tuple[float, float], tuple[float, float], tuple[float, float]]:
    verts: list[tuple[float, float, float]] = []
    faces = normals = uvs = 0
    for line in text.splitlines():
        if line.startswith("v "):
            parts = line.split()
            verts.append((float(parts[1]), float(parts[2]), float(parts[3])))
        elif line.startswith("vn "):
            normals += 1
        elif line.startswith("vt "):
            uvs += 1
        elif line.startswith("f "):
            faces += 1

    if not verts:
        return 0, faces, normals, uvs, (0, 0), (0, 0), (0, 0)

    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    return (
        len(verts),
        faces,
        normals,
        uvs,
        (min(xs), max(xs)),
        (min(ys), max(ys)),
        (min(zs), max(zs)),
    )


def extract_assets(zip_path: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        license_text = archive.read("License.txt")
        (output_dir / "LICENSE.txt").write_bytes(license_text)

        available = set(archive.namelist())
        for filename in VEHICLE_OBJS:
            source = f"Models/OBJ format/{filename}"
            if source not in available:
                raise FileNotFoundError(f"{source} not found in {zip_path}")
            data = archive.read(source)
            target = output_dir / filename
            target.write_bytes(data)

            stats = obj_stats(data.decode("utf-8", "ignore"))
            verts, faces, normals, uvs, bx, by, bz = stats
            print(
                f"{filename:20s} verts={verts:4d} faces={faces:4d} "
                f"vn={normals:3d} vt={uvs:3d} "
                f"bounds X={bx} Y={by} Z={bz}"
            )

    print(f"\nExtracted {len(VEHICLE_OBJS)} OBJ meshes to {output_dir}")
    print("License: CC0, see LICENSE.txt")


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=script_dir / "assets" / "kenney_car_kit",
        help="directory where selected OBJ files and LICENSE.txt are written",
    )
    parser.add_argument(
        "--zip-path",
        type=Path,
        default=Path(os.environ.get("TMPDIR", "/tmp")) / "kenney_car-kit.zip",
        help="cache path for the downloaded Kenney zip",
    )
    parser.add_argument(
        "--force-download",
        action="store_true",
        help="download even if --zip-path already exists",
    )
    args = parser.parse_args()

    if args.force_download or not args.zip_path.exists():
        download(DOWNLOAD_URL, args.zip_path)
    else:
        print(f"Using cached {args.zip_path}")

    extract_assets(args.zip_path, args.output_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
