#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

check_no_deferred_backend_state() {
    local file="$1"
    local forbidden='pass_count[[:space:]]*\+\+|pending_buffer_update_count[[:space:]]*\+\+'

    if grep -nE "$forbidden" "$file"; then
        echo "Deferred graphics pipeline state found in $file" >&2
        return 1
    fi
}

run_with_timeout() {
    local app="$1"
    local timeout_cmd=""
    local status=0

    if command -v timeout >/dev/null 2>&1; then
        timeout_cmd="timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        timeout_cmd="gtimeout"
    else
        echo "Skipping runtime smoke for $app: install timeout or coreutils for bounded runs."
        return 0
    fi

    echo "Runtime smoke: $app"
    set +e
    "$timeout_cmd" 5 ./build/budo "$app" --width 320 --height 240
    status=$?
    set -e

    if [[ "$status" -ne 0 && "$status" -ne 124 ]]; then
        echo "Runtime smoke failed for $app with status $status" >&2
        return "$status"
    fi
}

echo "Checking web and Android backends for removed deferred graphics state..."
check_no_deferred_backend_state src/web/web_window.c
check_no_deferred_backend_state private/android/android/app/src/main/cpp/android_window.c
python3 tests/gpu_boundary_invariants_test.py "$ROOT_DIR"

if [[ "${BUDO_SMOKE_BUILD:-1}" != "0" ]]; then
    echo "Building desktop runtime..."
    make build
else
    echo "Skipping desktop build because BUDO_SMOKE_BUILD=0."
fi

run_with_timeout examples/shader_demo
run_with_timeout examples/canvas_texture
run_with_timeout examples/3d_mesh_cube

if [[ "${BUDO_SMOKE_WEB:-1}" != "0" ]]; then
    echo "Building web runtime smoke app..."
    make web-build WEB_APP=examples/canvas_texture
else
    echo "Skipping web build because BUDO_SMOKE_WEB=0."
fi

if [[ "${BUDO_SMOKE_ANDROID:-0}" == "1" ]]; then
    echo "Building Android smoke APK..."
    ./build/budo android-apk examples/chess_clock --no-build
    (cd android && ./gradlew :app:assembleDebug)
else
    echo "Skipping Android build by default. Set BUDO_SMOKE_ANDROID=1 to enable it."
fi

echo "Graphics pipeline smoke checks passed."