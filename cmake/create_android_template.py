#!/usr/bin/env python3
"""Create the Android template tar payload embedded into the desktop binary."""

import os
import sys
import tarfile


EXCLUDED_PATHS = {
    "android/build",
    "android/.gradle",
    "android/.cxx",
    "android/app/build",
    "android/app/.cxx",
    "android/app/.gradle",
    "android/app/src/main/assets/app",
    "android/app/src/main/cpp/quickjs",
    "android/local.properties",
}


def should_exclude(rel_path: str) -> bool:
    rel_path = rel_path.replace(os.sep, "/")
    return any(rel_path == excluded or rel_path.startswith(excluded + "/") for excluded in EXCLUDED_PATHS)


def add_tree(archive: tarfile.TarFile, source_root: str, archive_root: str) -> None:
    for current_root, dirnames, filenames in os.walk(source_root):
        rel_from_source = os.path.relpath(current_root, source_root)
        if rel_from_source == ".":
            rel_root = archive_root
        else:
            rel_root = os.path.join(archive_root, rel_from_source).replace(os.sep, "/")

        dirnames[:] = [name for name in dirnames if not should_exclude(f"{rel_root}/{name}")]

        if not should_exclude(rel_root):
            archive.add(current_root, arcname=rel_root, recursive=False)

        for filename in filenames:
            abs_path = os.path.join(current_root, filename)
            rel_path = os.path.join(rel_root, filename).replace(os.sep, "/")
            if not should_exclude(rel_path):
                archive.add(abs_path, arcname=rel_path, recursive=False)


def main() -> int:
    if len(sys.argv) not in (3, 5):
        print(f"Usage: {sys.argv[0]} <repo_root> <output_tar> [--android-root <dir>]", file=sys.stderr)
        return 1

    repo_root = os.path.abspath(sys.argv[1])
    output_tar = os.path.abspath(sys.argv[2])
    android_root = os.path.join(repo_root, "android")
    if len(sys.argv) == 5:
        if sys.argv[3] != "--android-root":
            print(f"Unknown option: {sys.argv[3]}", file=sys.stderr)
            return 1
        android_root = os.path.abspath(sys.argv[4])

    os.makedirs(os.path.dirname(output_tar), exist_ok=True)

    with tarfile.open(output_tar, "w") as archive:
        add_tree(archive, android_root, "android")
        add_tree(archive, os.path.join(repo_root, "include"), "include")
        add_tree(archive, os.path.join(repo_root, "src"), "src")

    print(f"Created Android template tar: {output_tar}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
