#!/usr/bin/env python3
"""Download Kenney's CC0 Car Kit and generate native C mesh tables.

The JavaScript terrain example parses OBJ files at runtime. Native Android
assets are not regular files, so this native example performs the equivalent
normalization once and compiles all vehicle variants into the application.
"""

from __future__ import annotations

import argparse
import math
import os
import re
import urllib.request
import zipfile
from pathlib import Path

DOWNLOAD_URL = (
    "https://kenney.nl/media/pages/assets/car-kit/"
    "a9b1e99e92-1775131960/kenney_car-kit.zip"
)
VEHICLES = [
    "ambulance", "delivery-flat", "delivery", "firetruck", "garbage-truck",
    "hatchback-sports", "kart-oobi", "kart-oodi", "kart-ooli", "kart-oopi",
    "kart-oozi", "police", "race-future", "race", "sedan-sports", "sedan",
    "suv-luxury", "suv", "taxi", "tractor-police", "tractor-shovel",
    "tractor", "truck-flat", "truck", "van",
]


def download(path: Path, force: bool) -> None:
    if path.exists() and not force:
        print(f"Using cached {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {DOWNLOAD_URL}")
    with urllib.request.urlopen(DOWNLOAD_URL, timeout=60) as response:
        path.write_bytes(response.read())
    print(f"Wrote {path} ({path.stat().st_size / 1024:.1f} KiB)")


def parse_obj(text: str) -> tuple[list[tuple[float, float, float, float, float, float]], list[int]]:
    positions: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    vertices: list[tuple[float, float, float, float, float, float]] = []
    indices: list[int] = []
    cache: dict[tuple[int, int], int] = {}
    faces: list[list[tuple[int, int]]] = []
    for raw in text.splitlines():
        parts = raw.strip().split()
        if not parts or parts[0].startswith("#"):
            continue
        if parts[0] == "v":
            positions.append(tuple(map(float, parts[1:4])))
        elif parts[0] == "vn":
            normals.append(tuple(map(float, parts[1:4])))
        elif parts[0] == "f":
            face = []
            for item in parts[1:]:
                fields = item.split("/")
                pi = int(fields[0]) - 1
                ni = int(fields[2]) - 1 if len(fields) > 2 and fields[2] else -1
                face.append((pi, ni))
            faces.append(face)
    if not positions:
        raise ValueError("OBJ has no positions")
    xs, ys, zs = zip(*positions)
    cx, cz = (min(xs) + max(xs)) * .5, (min(zs) + max(zs)) * .5
    sx, sy, sz = max(max(xs) - min(xs), 1e-5), max(max(ys) - min(ys), 1e-5), max(max(zs) - min(zs), 1e-5)
    for face in faces:
        face_indices = []
        for key in face:
            if key not in cache:
                p = positions[key[0]]
                # Kenney X/Y/Z = right/up/length; Budo X/Y/Z = right/forward/up.
                px, py, pz = (p[0] - cx) / sx, (p[2] - cz) / sz, (p[1] - min(ys)) / sy - .5
                if key[1] >= 0:
                    n = normals[key[1]]
                    nx, ny, nz = n[0] / sx, n[2] / sz, n[1] / sy
                    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
                    nx, ny, nz = nx / length, ny / length, nz / length
                else:
                    nx, ny, nz = 0., 0., 1.
                cache[key] = len(vertices)
                vertices.append((px, py, pz, nx, ny, nz))
            face_indices.append(cache[key])
        for i in range(1, len(face_indices) - 1):
            # Axis swapping changes handedness.
            indices.extend((face_indices[0], face_indices[i + 1], face_indices[i]))
    if len(vertices) > 65535:
        raise ValueError(f"mesh has {len(vertices)} vertices; native SDK supports uint16 indices")
    return vertices, indices


def identifier(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def c_float(value: float) -> str:
    text = f"{value:.7g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def write_generated(models, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    header = output / "car_models.h"
    source = output / "car_models.c"
    header.write_text(
        "#ifndef NATIVE_TERRAIN_CAR_MODELS_H\n#define NATIVE_TERRAIN_CAR_MODELS_H\n\n"
        "#include <stddef.h>\n#include <stdint.h>\n\n"
        "typedef struct CarModelVertex { float position[3]; float normal[3]; } CarModelVertex;\n"
        "typedef struct CarModelData { const char *name; const CarModelVertex *vertices; "
        "size_t vertex_count; const uint16_t *indices; size_t index_count; } CarModelData;\n\n"
        "extern const CarModelData budo_car_models[];\n"
        "extern const size_t budo_car_model_count;\n\n#endif\n"
    )
    with source.open("w") as file:
        file.write('#include "car_models.h"\n\n')
        for name, vertices, indices in models:
            ident = identifier(name)
            file.write(f"static const CarModelVertex {ident}_vertices[] = {{\n")
            for v in vertices:
                values = [c_float(value) for value in v]
                file.write(
                    "    {{%s, %s, %s}, {%s, %s, %s}},\n" % tuple(values)
                )
            file.write("};\n")
            file.write(f"static const uint16_t {ident}_indices[] = {{\n    ")
            for offset in range(0, len(indices), 18):
                file.write(", ".join(map(str, indices[offset:offset + 18])))
                file.write(",\n    " if offset + 18 < len(indices) else "\n")
            file.write("};\n\n")
        file.write("const CarModelData budo_car_models[] = {\n")
        for name, vertices, indices in models:
            ident = identifier(name)
            file.write(f'    {{"{name}", {ident}_vertices, {len(vertices)}, {ident}_indices, {len(indices)}}},\n')
        file.write("};\nconst size_t budo_car_model_count = sizeof(budo_car_models) / sizeof(budo_car_models[0]);\n")
    print(f"Generated {header}")
    print(f"Generated {source} ({source.stat().st_size / 1024:.1f} KiB)")


def main() -> int:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip-path", type=Path, default=Path(os.environ.get("TMPDIR", "/tmp")) / "kenney_car-kit.zip")
    parser.add_argument("--force-download", action="store_true")
    args = parser.parse_args()
    download(args.zip_path, args.force_download)
    asset_dir = root / "assets" / "kenney_car_kit"
    models = []
    with zipfile.ZipFile(args.zip_path) as archive:
        asset_dir.mkdir(parents=True, exist_ok=True)
        (asset_dir / "LICENSE.txt").write_bytes(archive.read("License.txt"))
        for name in VEHICLES:
            data = archive.read(f"Models/OBJ format/{name}.obj")
            (asset_dir / f"{name}.obj").write_bytes(data)
            vertices, indices = parse_obj(data.decode("utf-8", "ignore"))
            models.append((name, vertices, indices))
            print(f"{name:20s} vertices={len(vertices):5d} indices={len(indices):5d}")
    write_generated(models, root / "generated")
    print(f"Extracted {len(models)} CC0 models to {asset_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
