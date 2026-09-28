#!/usr/bin/env python3
"""Behavioral tests for the optional Android feature-pack resolver."""

import importlib.util
import json
import shutil
import sys
import tempfile
from pathlib import Path


def load_resolver(root):
    path = root / "scripts" / "resolve-feature-pack.py"
    spec = importlib.util.spec_from_file_location("resolve_feature_pack", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_pack(root, feature_id="android-packaging", minimum="0.4.3",
              maximum="0.4.3"):
    pack = root / "pack"
    for directory in ("android", "src", "make", "scripts", "android-packaging"):
        (pack / directory).mkdir(parents=True, exist_ok=True)
    (pack / "src" / "android_package.c").write_text("void package(void) {}\n")
    (pack / "make" / "android.mk").write_text("android-build:\n\t@true\n")
    (pack / "android" / "CMakeLists.txt").write_text("# fixture\n")
    (pack / "scripts" / "bundle.sh").write_text("#!/bin/sh\n")
    (pack / "android-packaging" / "app.json.example").write_text("{}\n")
    manifest = {
        "format_version": 1,
        "feature_id": feature_id,
        "feature_version": "1.0.0",
        "host_compatibility": {"minimum": minimum, "maximum": maximum},
        "entrypoints": {
            "package_source": "src/android_package.c",
            "template_root": "android",
            "source_include": "src",
            "make_include": "make/android.mk",
        },
        "required_paths": [
            "android/CMakeLists.txt",
            "scripts/bundle.sh",
            "android-packaging/app.json.example",
        ],
        "make": {"everything_targets": ["android-support-release"]},
    }
    (pack / "feature-pack.json").write_text(json.dumps(manifest))
    return pack


def main():
    source_root = Path(sys.argv[1]).resolve()
    resolver = load_resolver(source_root)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        repo = root / "repo"
        (repo / "include" / "budo").mkdir(parents=True)
        shutil.copy(source_root / "include" / "budo" / "version.h",
                    repo / "include" / "budo" / "version.h")

        assert resolver.load_pack(repo, root / "missing") is None
        absent_make = resolver.render_make(None, root / "missing")
        assert "BUDO_ANDROID_PACK_AVAILABLE := 0" in absent_make
        assert "BUDO_ENABLE_ANDROID_PACKAGING=OFF" in absent_make

        pack = make_pack(root)
        resolved = resolver.load_pack(repo, pack)
        assert resolved["feature_version"] == "1.0.0"
        assert resolved["package_source"].name == "android_package.c"
        assert "BUDO_ANDROID_PACK_AVAILABLE := 1" in resolver.render_make(resolved, pack)
        assert 'set(BUDO_ANDROID_PACK_AVAILABLE "ON")' in resolver.render_cmake(resolved)

        manifest_path = pack / "feature-pack.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["feature_id"] = "wrong-feature"
        manifest_path.write_text(json.dumps(manifest))
        try:
            resolver.load_pack(repo, pack)
            assert False, "wrong feature id was accepted"
        except ValueError as error:
            assert "unexpected feature_id" in str(error)

        pack = make_pack(root, minimum="0.5.0", maximum="0.6.0")
        try:
            resolver.load_pack(repo, pack)
            assert False, "incompatible host version was accepted"
        except ValueError as error:
            assert "host is 0.4.3" in str(error)

        pack = make_pack(root)
        (pack / "src" / "android_package.c").unlink()
        try:
            resolver.load_pack(repo, pack)
            assert False, "missing entry point was accepted"
        except ValueError as error:
            assert "package_source file not found" in str(error)

    print("feature pack resolver tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
