#!/usr/bin/env python3

import json
import pathlib
import sys


SHARED_COMPOSITION = "src/core/managed_subsystems.c"

HOSTS = (
    "src/desktop/managed_desktop.c",
    "src/web/web_main.c",
    "private/android/android/app/src/main/cpp/android_main.cpp",
)


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    errors = []

    manifest_path = root / "api/generated/registration-manifest.json"
    if not manifest_path.is_file():
        errors.append("missing generated registration manifest")
        registrations = []
    else:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("version") != 1:
            errors.append("unsupported registration manifest version")
        registrations = manifest.get("registrations", [])

    for registration in registrations:
        source_path = root / registration["source"]
        if not source_path.is_file():
            errors.append(
                f"{registration['subsystem']}: missing registration source "
                f"{registration['source']}")
            continue
        source = source_path.read_text(encoding="utf-8")
        symbol = registration["symbol"]
        if symbol + "(" not in source:
            errors.append(
                f"{registration['subsystem']}/{registration['runtime']}: "
                f"registration symbol {symbol} missing from {registration['source']}")

    shared = (root / SHARED_COMPOSITION).read_text(encoding="utf-8")
    if shared.count("subsystem_compose(") != 1:
        errors.append(
            f"{SHARED_COMPOSITION}: expected exactly one subsystem_compose call")

    for relative_path in HOSTS + (SHARED_COMPOSITION,):
        source = (root / relative_path).read_text(encoding="utf-8")
        if relative_path != SHARED_COMPOSITION and \
                source.count("managed_subsystems_compose(") != 1:
            errors.append(
                f"{relative_path}: expected exactly one managed_subsystems_compose call")
        if "subsystem_registry_register(" in source:
            errors.append(
                f"{relative_path}: host must not register subsystems directly")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(
        "subsystem_composition_invariants_test: "
        f"ok ({len(registrations)} source-backed registrations)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())