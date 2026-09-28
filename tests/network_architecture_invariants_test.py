#!/usr/bin/env python3
"""Validate network ownership boundaries and platform source lists."""

import sys
from pathlib import Path


root = Path(sys.argv[1])
root_cmake = (root / "CMakeLists.txt").read_text()
web_cmake = (root / "web/CMakeLists.txt").read_text()
android_cmake = (
    root / "private/android/android/app/src/main/cpp/CMakeLists.txt"
).read_text()
wrapper = (root / "src/network/network_wrapper.c").read_text()
policy = (root / "src/network/network_policy.c").read_text()
request_core = (root / "src/network/network_request_core.c").read_text()

shared_sources = ("network_policy.c", "network_request_core.c")
for source in shared_sources:
    assert source in root_cmake, f"desktop build omits {source}"
    assert source in web_cmake, f"web build omits {source}"
    assert source in android_cmake, f"Android build omits {source}"

assert "network_desktop_transport.c" in root_cmake
assert "network_desktop_transport.c" not in web_cmake
assert "network_desktop_transport.c" not in android_cmake

assert "network_policy_allows_url" in policy
assert "network_request_validate" in request_core
assert "network_request_plan_redirect" in request_core
assert "network_policy_allows_url" in wrapper
assert "network_request_validate" in wrapper
assert "network_request_plan_redirect" in wrapper

for binding in (
    "js_network_bindings.c",
    "lua_network_bindings.c",
    "wasm_network_bindings.c",
):
    source = (root / "src/network" / binding).read_text()
    assert source.count("network_policy_load_app_json(") == 1, binding
    assert "static void load_policy_from_app_json" not in source, binding
    assert "static void lua_load_policy_from_app_json" not in source, binding

print("network architecture invariants passed")
