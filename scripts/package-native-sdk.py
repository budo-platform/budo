#!/usr/bin/env python3
"""Validate, manifest, and deterministically archive a staged Budo native SDK."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tarfile
import time
import zipfile

REQUIRED = (
    "include/budo/budo.h",
    "lib/cmake/BudoNative/BudoNativeConfig.cmake",
    "lib/cmake/BudoNative/BudoNativeConfigVersion.cmake",
    "lib/cmake/BudoNative/BudoNativeTargets.cmake",
    "share/budo/LICENSE",
    "share/budo/THIRD_PARTY_NOTICES.md",
)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def members(root: Path) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        relative = path.relative_to(root).as_posix()
        if relative == "share/budo/budo-native-sdk.json":
            continue
        if path.is_symlink():
            raise SystemExit(f"SDK must not contain symlinks: {relative}")
        if path.is_file():
            result.append({"path": relative, "size": path.stat().st_size, "sha256": digest(path)})
    return result


def canonical(data: object) -> bytes:
    return (json.dumps(data, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode()


def write_tar(root: Path, output: Path, epoch: int) -> None:
    top = output.name.removesuffix(".tar.gz")
    with output.open("wb") as raw:
        import gzip
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as zipped:
            # The installed CLI intentionally implements the small, auditable
            # USTAR regular-file/directory subset. PAX/GNU extension records,
            # links, devices, and other special members are not distribution
            # inputs and must never be needed to unpack an SDK.
            with tarfile.open(fileobj=zipped, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
                    relative = Path(top) / path.relative_to(root)
                    info = archive.gettarinfo(str(path), str(relative))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = epoch
                    info.mode = 0o755 if path.is_dir() or os.access(path, os.X_OK) else 0o644
                    if path.is_file():
                        with path.open("rb") as stream:
                            archive.addfile(info, stream)
                    else:
                        archive.addfile(info)


def write_zip(root: Path, output: Path, epoch: int) -> None:
    top = output.stem
    timestamp = time.gmtime(max(epoch, 315532800))[:6]
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
            if not path.is_file():
                continue
            name = (Path(top) / path.relative_to(root)).as_posix()
            info = zipfile.ZipInfo(name, timestamp)
            info.external_attr = (0o755 if os.access(path, os.X_OK) else 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, path.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tuple", required=True)
    parser.add_argument("--build-id", default="development")
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--artifact-url", default="",
                        help="immutable absolute archive URL recorded in release metadata")
    args = parser.parse_args()
    root = args.stage.resolve()
    for required in REQUIRED:
        if not (root / required).is_file():
            raise SystemExit(f"Incomplete native SDK: missing {required}")
    files = members(root)
    input_digest = hashlib.sha256(canonical(files)).hexdigest()
    manifest = {
        "schema_version": 1,
        "budo_version": args.version,
        "native_api_version": "1.0",
        "sdk_build_id": args.build_id,
        "target_tuple": args.tuple,
        "compiler_family": args.compiler,
        "platform_baseline": args.baseline,
        "debug_information": "RelWithDebInfo; symbols retained; deterministic path remapping required for releases",
        "sdk_input_digest": input_digest,
        "files": files,
    }
    manifest_path = root / "share/budo/budo-native-sdk.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_bytes(canonical(manifest))
    # Reject the most common accidental checkout leakage in package metadata.
    forbidden = (str(Path.cwd().resolve()), str(root.parent.resolve()))
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in {".cmake", ".json"}:
            text = path.read_text(errors="ignore")
            for prefix in forbidden:
                if prefix and prefix in text:
                    raise SystemExit(f"Non-relocatable path leaked into {path.relative_to(root)}: {prefix}")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    if output.suffix == ".zip":
        write_zip(root, output, epoch)
    elif output.name.endswith(".tar.gz"):
        write_tar(root, output, epoch)
    else:
        raise SystemExit("Output must end in .tar.gz or .zip")
    artifact = {
            "target_tuple": args.tuple,
            "filename": output.name,
            "size": output.stat().st_size,
            "sha256": digest(output),
            "sdk_manifest_sha256": digest(manifest_path),
            "sdk_input_digest": input_digest,
        }
    if args.artifact_url:
        artifact["url"] = args.artifact_url
    release = {
        "schema_version": 1,
        "budo_version": args.version,
        "artifacts": [artifact],
    }
    output.with_suffix(output.suffix + ".release.json").write_bytes(canonical(release))
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
