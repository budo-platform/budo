#!/usr/bin/env python3
"""Static contract checks for both browser network entry paths."""

from pathlib import Path
import sys


root = Path(sys.argv[1])
main = (root / "src/web/web_main.c").read_text()
bridge = (root / "src/web/web_network.c").read_text()
header = (root / "src/network/network_wrapper.h").read_text()
web_cmake = (root / "web/CMakeLists.txt").read_text()

guest_fetch = main.split("network_fetch: (up, ul, mp, ml, hp, hl, bp, bl) => {", 1)[1]
guest_fetch = guest_fetch.split("network_fetch_status:", 1)[0]

assert "fetch(" not in guest_fetch, "browser-native WASM must not call fetch directly"
assert "budo_web_wasm_network_fetch" in guest_fetch
assert "network_request_async(" in main
assert "network_policy_load_app_json(&policy, \"/\")" in main
assert "network_async_poll(state->net_ctx)" in main
assert "budo_web_wasm_network_shutdown" in main
assert "managed_js_network_poll" in main
assert "managed_lua_network_poll" in main
assert "subsystem_registry_poll(&state->subsystems)" in main
assert "js_network_context(state->js_network_ctx)" in main
assert "lua_network_context(state->lua_network_ctx)" in main

for shared_limit in (
    "NETWORK_MAX_RESPONSE_HEADERS_SIZE",
    "NETWORK_MAX_RESPONSE_BODY_SIZE",
    "NETWORK_MAX_URL_SIZE",
    "NETWORK_MAX_REDIRECTS",
    "NETWORK_REQUEST_TIMEOUT_MS",
):
    assert shared_limit in header
    assert shared_limit in bridge, f"managed web fetch must use {shared_limit}"

assert "redirect : allow_all ? 'follow' : 'manual'" in bridge
assert "redirectCount >= max_redirects" in bridge
assert "_web_network_is_url_allowed" in bridge
assert "response.type === 'opaqueredirect'" in bridge
assert "implementation-defined" in bridge
assert "setTimeout(" in bridge and "controller.abort()" in bridge
for header in ("authorization", "cookie", "proxy-authorization"):
    assert f"nextOpts.headers.delete('{header}')" in bridge
assert "name.toLowerCase().indexOf('content-') === 0" in bridge
assert "_budo_web_wasm_network_fetch" in web_cmake
assert "_budo_web_wasm_network_shutdown" in web_cmake
assert "function validRange(ptr, len)" in main
assert "len <= size - ptr" in main
assert "up + ul" not in guest_fetch and "ptr + maxLen" not in guest_fetch

print("web network policy consistency checks passed")