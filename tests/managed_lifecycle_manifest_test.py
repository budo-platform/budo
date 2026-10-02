#!/usr/bin/env python3
"""Verify managed subsystem lifecycle registration across platform hosts."""

import json
import re
import sys
from collections import Counter
from pathlib import Path


MANUAL_POLLS = {
    "desktop": (
        "js_midi_poll(runtime->",
        "js_file_poll(runtime->",
        "js_network_poll(runtime->",
        "lua_midi_poll(runtime->",
        "lua_network_poll(runtime->",
    ),
    "web": (
        "js_midi_poll(state->",
        "js_file_poll(state->",
        "js_network_poll(state->",
        "lua_midi_poll(state->",
        "lua_network_poll(state->",
    ),
    "android": (
        "js_midi_poll(g_state.",
        "js_file_poll(g_state.",
        "js_udp_poll(g_state.",
        "js_network_poll(g_state.",
        "lua_midi_poll(g_state.",
        "lua_udp_poll(g_state.",
        "lua_network_poll(g_state.",
    ),
}


def registered_names(source, function):
    # MANAGED_SUBSYSTEM_BUSY declares the same registration plus pending work.
    pattern = re.compile(rf"\b{re.escape(function)}(?:_BUSY)?\s*\(\s*\"([^\"]+)\"")
    return pattern.findall(source)


def registration_arguments(source, function, name):
    # Descriptor macro arguments are plain identifiers and constants, so the
    # invocation ends at its first closing parenthesis.
    pattern = re.compile(
        rf"\b{re.escape(function)}(?:_BUSY)?\s*\(\s*\"{re.escape(name)}\"\s*,(?P<arguments>[^)]*)\)")
    match = pattern.search(source)
    return match.group("arguments") if match else ""


def check_table(errors, label, source, function, runtimes, polls):
    actual = Counter(registered_names(source, function))
    expected = Counter(name for names in runtimes.values() for name in names)
    if actual != expected:
        missing = sorted((expected - actual).elements())
        extra = sorted((actual - expected).elements())
        if missing:
            errors.append(f"{label}: missing registrations: {', '.join(missing)}")
        if extra:
            errors.append(f"{label}: unmanifested registrations: {', '.join(extra)}")

    for name, poll_adapter in polls.items():
        arguments = registration_arguments(source, function, name)
        if not re.search(rf"\b{re.escape(poll_adapter)}\b", arguments):
            errors.append(f"{label}: {name} missing poll adapter {poll_adapter}")


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    manifest_path = root / "api" / "managed-lifecycle.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    errors = []
    if manifest.get("version") != 2:
        errors.append("managed lifecycle manifest version must be 2")

    shared = manifest.get("shared", {})
    shared_source = (root / shared["source"]).read_text(encoding="utf-8")
    check_table(errors, "shared", shared_source, shared["register_function"],
                shared.get("runtimes", {}), shared.get("poll", {}))
    if "subsystem_compose(" not in shared_source:
        errors.append("shared: tables are not composed through subsystem_compose")

    compose_call = shared["compose_function"] + "("
    for host, details in manifest.get("hosts", {}).items():
        source = (root / details["source"]).read_text(encoding="utf-8")
        if "register_function" in details:
            check_table(errors, host, source, details["register_function"],
                        details.get("runtimes", {}), details.get("poll", {}))
        if source.count(compose_call) != 1:
            errors.append(f"{host}: expected exactly one {compose_call}...) call")
        if "managed_runtime_shutdown(" not in source:
            errors.append(f"{host}: missing registry shutdown")
        if "managed_runtime_frame(" not in source:
            errors.append(f"{host}: missing registry poll dispatch")
        for call in MANUAL_POLLS.get(host, ()):
            if call in source:
                errors.append(f"{host}: manual poll bypass remains: {call}")

    desktop = (root / "src" / "desktop" / "managed_desktop.c").read_text(encoding="utf-8")
    for field in ("pause", "resume", "context_lost"):
        if not re.search(rf"\.{field}\s*=\s*runtime_adapter_{field}", desktop):
            errors.append(f"desktop: ApplicationDriver does not forward {field}")

    android = (root / "private" / "android" / "android" / "app" / "src" /
               "main" / "cpp" / "android_main.cpp").read_text(encoding="utf-8")
    for field in ("pause", "resume", "context_lost"):
        if not re.search(rf"\.{field}\s*=\s*android_runtime_{field}", android):
            errors.append(f"android: ApplicationDriver does not forward {field}")
        if f"subsystem_registry_{field}" not in android:
            errors.append(f"android: registry does not receive {field}")

    if re.search(r"\b(js|lua)_(midi|audio|sqlite|file|network|magneto|udp)_cleanup\s*\(",
                 android):
        errors.append("android: manual managed cleanup call remains")
    if "js_runtime_destroy(g_state" in android or "lua_canvas_destroy(g_state" in android:
        errors.append("android: manual canvas cleanup call remains")

    if errors:
        for error in errors:
            print(f"managed_lifecycle_manifest_test: {error}", file=sys.stderr)
        return 1
    print("managed lifecycle manifest checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
