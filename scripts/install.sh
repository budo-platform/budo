#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
BUILD_DIR="${1:-build}"
MAKE_COMMAND="${MAKE_COMMAND:-make}"
MAKE_ENV=(
    "BUILD_DIR=$BUILD_DIR"
    "BUILD_TYPE=${BUILD_TYPE:-Release}"
    "CMAKE_FLAGS=${CMAKE_FLAGS:-}"
    "QUICKJS_IMPL=${QUICKJS_IMPL:-}"
    "BUDO_ANDROID_PACK_ROOT=${BUDO_ANDROID_PACK_ROOT:-private/android}"
)

cd "$PROJECT_ROOT"

build_as_current_user() {
    env "${MAKE_ENV[@]}" "$MAKE_COMMAND" --no-print-directory build
}

if [[ "$(id -u)" -ne 0 || -z "${SUDO_USER:-}" || "$SUDO_USER" == "root" ]]; then
    build_as_current_user
    cmake --install "$BUILD_DIR"
    exit 0
fi

if [[ ! "${SUDO_UID:-}" =~ ^[0-9]+$ || ! "${SUDO_GID:-}" =~ ^[0-9]+$ ]]; then
    echo "Invalid or missing SUDO_UID/SUDO_GID; refusing to change build ownership." >&2
    exit 1
fi

repair_build_ownership() {
    [[ -d "$BUILD_DIR" ]] || return 0
    if [[ -L "$BUILD_DIR" ]]; then
        echo "Build directory must not be a symbolic link: $BUILD_DIR" >&2
        return 1
    fi

    local build_root
    build_root="$(cd "$BUILD_DIR" && pwd -P)"
    if [[ "$build_root" != "$PROJECT_ROOT"/* ]]; then
        echo "Build directory must be inside the project: $build_root" >&2
        return 1
    fi

    find "$build_root" -user 0 -exec chown -h "$SUDO_UID:$SUDO_GID" {} +
}

repair_build_ownership
echo "Building as $SUDO_USER before privileged install..."
sudo -u "$SUDO_USER" -H env "${MAKE_ENV[@]}" "$MAKE_COMMAND" --no-print-directory build

install_status=0
cmake --install "$BUILD_DIR" || install_status=$?
repair_build_ownership
exit "$install_status"
