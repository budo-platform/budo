#!/usr/bin/env python3
"""Resolve and validate an optional Budo feature pack for Make or CMake."""

import argparse
import json
import re
import shlex
import sys
from pathlib import Path


SUPPORTED_FORMAT_VERSION = 1
ANDROID_FEATURE_ID = "android-packaging"


def host_version(repo):
    header = (repo / "include" / "budo" / "version.h").read_text(encoding="utf-8")
    match = re.search(r'#define\s+BUDO_VERSION_STRING\s+"([^"]+)"', header)
    if not match:
        raise ValueError("BUDO_VERSION_STRING not found in include/budo/version.h")
    return match.group(1)


def version_tuple(value):
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value)
    if not match:
        raise ValueError(f"invalid semantic version: {value}")
    return tuple(int(part) for part in match.groups())


def require_relative_file(pack_root, relative, label, directory=False):
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise ValueError(f"{label} must be a non-empty pack-relative path")
    path = (pack_root / relative).resolve()
    try:
        path.relative_to(pack_root.resolve())
    except ValueError as error:
        raise ValueError(f"{label} escapes the feature pack: {relative}") from error
    exists = path.is_dir() if directory else path.is_file()
    if not exists:
        kind = "directory" if directory else "file"
        raise ValueError(f"{label} {kind} not found: {path}")
    return path


def load_pack(repo, pack_root):
    manifest_path = pack_root / "feature-pack.json"
    if not manifest_path.is_file():
        return None
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format_version") != SUPPORTED_FORMAT_VERSION:
        raise ValueError(
            f"unsupported feature-pack format_version: {manifest.get('format_version')}"
        )
    if manifest.get("feature_id") != ANDROID_FEATURE_ID:
        raise ValueError(f"unexpected feature_id: {manifest.get('feature_id')}")

    compatibility = manifest.get("host_compatibility")
    if not isinstance(compatibility, dict):
        raise ValueError("host_compatibility must be an object")
    current = version_tuple(host_version(repo))
    minimum = version_tuple(compatibility.get("minimum", ""))
    maximum = version_tuple(compatibility.get("maximum", ""))
    if not minimum <= current <= maximum:
        raise ValueError(
            f"feature pack supports Budo {compatibility['minimum']} through "
            f"{compatibility['maximum']}, host is {'.'.join(map(str, current))}"
        )

    entrypoints = manifest.get("entrypoints")
    if not isinstance(entrypoints, dict):
        raise ValueError("entrypoints must be an object")
    resolved = {
        "manifest": manifest_path.resolve(),
        "root": pack_root.resolve(),
        "package_source": require_relative_file(
            pack_root, entrypoints.get("package_source"),
            "entrypoints.package_source"),
        "template_root": require_relative_file(
            pack_root, entrypoints.get("template_root"),
            "entrypoints.template_root", directory=True),
        "source_include": require_relative_file(
            pack_root, entrypoints.get("source_include"),
            "entrypoints.source_include", directory=True),
        "make_include": require_relative_file(
            pack_root, entrypoints.get("make_include"),
            "entrypoints.make_include"),
    }
    required_paths = manifest.get("required_paths")
    if not isinstance(required_paths, list) or not required_paths:
        raise ValueError("required_paths must be a non-empty array")
    for index, relative in enumerate(required_paths):
        if not isinstance(relative, str) or not relative:
            raise ValueError(f"required_paths[{index}] must be a non-empty string")
        path = (pack_root / relative).resolve()
        try:
            path.relative_to(pack_root.resolve())
        except ValueError as error:
            raise ValueError(f"required_paths[{index}] escapes the pack") from error
        if not path.exists():
            raise ValueError(f"required feature-pack path not found: {path}")

    targets = manifest.get("make", {}).get("everything_targets")
    if not isinstance(targets, list) or any(
            not isinstance(target, str) or not target for target in targets):
        raise ValueError("make.everything_targets must be an array of target names")
    resolved["everything_targets"] = targets
    resolved["feature_version"] = manifest.get("feature_version")
    if not isinstance(resolved["feature_version"], str) or not resolved["feature_version"]:
        raise ValueError("feature_version must be a non-empty string")
    return resolved


def cmake_quote(value):
    return str(value).replace("\\", "/").replace('"', '\\"')


def render_cmake(resolved):
    if resolved is None:
        return "\n".join([
            "set(BUDO_ANDROID_PACK_AVAILABLE OFF)",
            "set(BUDO_ANDROID_PACK_MANIFEST \"\")",
            "set(BUDO_ANDROID_PACK_PACKAGE_SOURCE \"\")",
            "set(BUDO_ANDROID_PACK_TEMPLATE_ROOT \"\")",
            "set(BUDO_ANDROID_PACK_SOURCE_INCLUDE \"\")",
            "set(BUDO_ANDROID_PACK_MAKE_INCLUDE \"\")",
            "set(BUDO_ANDROID_PACK_EVERYTHING_TARGETS \"\")",
            "",
        ])
    targets = ";".join(resolved["everything_targets"])
    values = {
        "BUDO_ANDROID_PACK_AVAILABLE": "ON",
        "BUDO_ANDROID_PACK_MANIFEST": resolved["manifest"],
        "BUDO_ANDROID_PACK_PACKAGE_SOURCE": resolved["package_source"],
        "BUDO_ANDROID_PACK_TEMPLATE_ROOT": resolved["template_root"],
        "BUDO_ANDROID_PACK_SOURCE_INCLUDE": resolved["source_include"],
        "BUDO_ANDROID_PACK_MAKE_INCLUDE": resolved["make_include"],
        "BUDO_ANDROID_PACK_EVERYTHING_TARGETS": targets,
        "BUDO_ANDROID_PACK_FEATURE_VERSION": resolved["feature_version"],
    }
    return "\n".join(
        f'set({name} "{cmake_quote(value)}")' for name, value in values.items()
    ) + "\n"


def make_quote(value):
    return shlex.quote(str(value))


def render_make(resolved, pack_root):
    if resolved is None:
        return "\n".join([
            "BUDO_ANDROID_PACK_AVAILABLE := 0",
            f"BUDO_ANDROID_PACK_ROOT_RESOLVED := {pack_root.resolve()}",
            "BUDO_ANDROID_CMAKE_FLAGS := -DBUDO_ENABLE_ANDROID_PACKAGING=OFF",
            "BUDO_ANDROID_PACK_MAKE_INCLUDE :=",
            "BUDO_ANDROID_EVERYTHING_TARGETS :=",
            "",
        ])
    targets = " ".join(resolved["everything_targets"])
    return "\n".join([
        "BUDO_ANDROID_PACK_AVAILABLE := 1",
        f"BUDO_ANDROID_PACK_ROOT_RESOLVED := {resolved['root']}",
        f"BUDO_ANDROID_PACK_MANIFEST := {resolved['manifest']}",
        f"BUDO_ANDROID_PACK_PACKAGE_SOURCE := {resolved['package_source']}",
        f"BUDO_ANDROID_PACK_TEMPLATE_ROOT := {resolved['template_root']}",
        f"BUDO_ANDROID_PACK_SOURCE_INCLUDE := {resolved['source_include']}",
        "BUDO_ANDROID_CMAKE_FLAGS := "
        f"-DBUDO_ENABLE_ANDROID_PACKAGING=ON "
        f"-DBUDO_ANDROID_PACK_ROOT={make_quote(resolved['root'])}",
        f"BUDO_ANDROID_PACK_MAKE_INCLUDE := {resolved['make_include']}",
        f"BUDO_ANDROID_EVERYTHING_TARGETS := {targets}",
        "",
    ])


def write_if_changed(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--pack-root", type=Path)
    parser.add_argument("--format", choices=("cmake", "make"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    pack_root = (args.pack_root or repo / "private" / "android")
    if not pack_root.is_absolute():
        pack_root = (repo / pack_root).resolve()
    try:
        resolved = load_pack(repo, pack_root)
        content = (render_cmake(resolved) if args.format == "cmake"
                   else render_make(resolved, pack_root))
        write_if_changed(args.output, content)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Android feature-pack resolution failed: {error}", file=sys.stderr)
        return 1
    status = "available" if resolved else "unavailable"
    print(f"Android feature pack: {status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
