#!/usr/bin/env python3
"""Enforce one feature-pack resolver across Make and CMake build graphs."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_make(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"([A-Z0-9_]+)\s*:=\s*(.*)", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def parse_cmake(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r'set\(([A-Z0-9_]+) "(.*)"\)', line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    make_source = (root / "Makefile").read_text(encoding="utf-8")
    cmake_source = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    errors = []
    for label, source in (("Makefile", make_source), ("CMakeLists.txt", cmake_source)):
        if "scripts/resolve-feature-pack.py" not in source:
            errors.append(f"{label}: shared feature-pack resolver is not used")
        for legacy in (
            "src/core/android_package.c",
            "EXISTS \"${BUDO_ANDROID_PACK_ROOT}/android\"",
            "wildcard $(BUDO_ANDROID_PACK_ROOT)/make/android.mk",
        ):
            if legacy in source:
                errors.append(f"{label}: legacy Android pack detection remains: {legacy}")

    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        make_out = temporary / "pack.mk"
        cmake_out = temporary / "pack.cmake"
        base = [
            sys.executable, str(root / "scripts" / "resolve-feature-pack.py"),
            "--repo", str(root), "--pack-root", str(root / "private" / "android"),
        ]
        subprocess.run(base + ["--format", "make", "--output", str(make_out)],
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run(base + ["--format", "cmake", "--output", str(cmake_out)],
                       check=True, stdout=subprocess.DEVNULL)
        make_values = parse_make(make_out)
        cmake_values = parse_cmake(cmake_out)
        pairs = (
            ("BUDO_ANDROID_PACK_MANIFEST", "BUDO_ANDROID_PACK_MANIFEST"),
            ("BUDO_ANDROID_PACK_PACKAGE_SOURCE", "BUDO_ANDROID_PACK_PACKAGE_SOURCE"),
            ("BUDO_ANDROID_PACK_TEMPLATE_ROOT", "BUDO_ANDROID_PACK_TEMPLATE_ROOT"),
            ("BUDO_ANDROID_PACK_SOURCE_INCLUDE", "BUDO_ANDROID_PACK_SOURCE_INCLUDE"),
            ("BUDO_ANDROID_PACK_MAKE_INCLUDE", "BUDO_ANDROID_PACK_MAKE_INCLUDE"),
            ("BUDO_ANDROID_EVERYTHING_TARGETS", "BUDO_ANDROID_PACK_EVERYTHING_TARGETS"),
        )
        for make_name, cmake_name in pairs:
            if make_values.get(make_name) != cmake_values.get(cmake_name):
                errors.append(
                    f"resolved mismatch: {make_name}={make_values.get(make_name)!r}, "
                    f"{cmake_name}={cmake_values.get(cmake_name)!r}"
                )
        if make_values.get("BUDO_ANDROID_PACK_AVAILABLE") != "1" or \
                cmake_values.get("BUDO_ANDROID_PACK_AVAILABLE") != "ON":
            errors.append("installed Android feature pack was not available in both outputs")

    if errors:
        for error in errors:
            print(f"feature_pack_build_boundary_test: {error}", file=sys.stderr)
        return 1
    print("feature pack build boundary checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
