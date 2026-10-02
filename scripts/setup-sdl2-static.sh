#!/usr/bin/env bash
# Build SDL2 from the pinned source release as a static library, so release
# binaries do not depend on an SDL2 installed by Homebrew or the distribution.
#
#   ./scripts/setup-sdl2-static.sh
#   make build CMAKE_FLAGS="-DCMAKE_PREFIX_PATH=$PWD/third_party/sdl2-static"
#
# On Linux, SDL still loads X11, Wayland, ALSA, and PulseAudio at runtime, so
# their development headers are needed to build but the binary does not link
# them. Idempotent: re-running is a no-op unless FORCE=1 is set.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

SDL2_VERSION="$(budo_dependency_lock_value BUDO_SDL2_SOURCE_VERSION)"
SDL2_URL="$(budo_dependency_lock_value BUDO_SDL2_SOURCE_URL)"
SDL2_SHA256="$(budo_dependency_lock_value BUDO_SDL2_SOURCE_SHA256)"
SDL2_PREFIX="${SDL2_PREFIX:-$PROJECT_ROOT/third_party/sdl2-static}"
SDL2_STAMP="$SDL2_PREFIX/.budo-sdl2-version"

echo_step "Static SDL2 $SDL2_VERSION"

if [ -f "$SDL2_STAMP" ] && [ "$(cat "$SDL2_STAMP")" = "$SDL2_VERSION" ] && [ "${FORCE:-0}" != "1" ]; then
    echo_info "Already installed in $SDL2_PREFIX (set FORCE=1 to rebuild)"
    exit 0
fi

require_cmd curl
require_cmd cmake

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

archive="$TMP_DIR/SDL2-$SDL2_VERSION.tar.gz"
echo_info "Downloading $SDL2_URL"
curl -fsSL --proto '=https' --tlsv1.2 -o "$archive" "$SDL2_URL"

if command -v sha256sum >/dev/null 2>&1; then
    actual_sha="$(sha256sum "$archive" | awk '{print $1}')"
else
    actual_sha="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi
if [ "$actual_sha" != "$SDL2_SHA256" ]; then
    echo_error "Checksum mismatch for SDL2 $SDL2_VERSION"
    echo_error "  expected: $SDL2_SHA256"
    echo_error "  actual:   $actual_sha"
    exit 1
fi
echo_info "Checksum OK"

tar -xzf "$archive" -C "$TMP_DIR"
jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

rm -rf "$SDL2_PREFIX"
cmake -S "$TMP_DIR/SDL2-$SDL2_VERSION" -B "$TMP_DIR/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SDL2_PREFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DSDL_SHARED=OFF \
    -DSDL_STATIC=ON \
    -DSDL_TEST=OFF
cmake --build "$TMP_DIR/build" -j"$jobs"
cmake --install "$TMP_DIR/build"
printf '%s\n' "$SDL2_VERSION" > "$SDL2_STAMP"

echo_info "Installed: $SDL2_PREFIX"
echo_step "Done."
