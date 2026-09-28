#!/usr/bin/env bash
# scripts/setup-onnx.sh — Download ORT prebuilt C API for the current desktop platform.
# Usage: ./scripts/setup-onnx.sh [version]
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

LOCKED_ORT_VERSION="$(budo_dependency_lock_value BUDO_ONNXRUNTIME_VERSION)"
ORT_VERSION=${1:-"$LOCKED_ORT_VERSION"}
if [[ "$ORT_VERSION" != "$LOCKED_ORT_VERSION" ]]; then
    echo "Refusing unpinned ONNX Runtime version $ORT_VERSION (locked: $LOCKED_ORT_VERSION)" >&2
    exit 1
fi
PLATFORM=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

if [[ "$PLATFORM" == "darwin" ]]; then
    if [[ "$ARCH" == "arm64" ]]; then
        TARBALL="onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
        ORT_HASH="$(budo_dependency_lock_value BUDO_ONNXRUNTIME_OSX_ARM64_URL_HASH)"
    else
        TARBALL="onnxruntime-osx-x86_64-${ORT_VERSION}.tgz"
        ORT_HASH="$(budo_dependency_lock_value BUDO_ONNXRUNTIME_OSX_X86_64_URL_HASH)"
    fi
elif [[ "$PLATFORM" == "linux" ]]; then
    if [[ "$ARCH" != "x86_64" ]]; then
        echo "Unsupported Linux architecture: $ARCH" >&2
        exit 1
    fi
    TARBALL="onnxruntime-linux-x64-${ORT_VERSION}.tgz"
    ORT_HASH="$(budo_dependency_lock_value BUDO_ONNXRUNTIME_LINUX_X64_URL_HASH)"
else
    echo "Unsupported platform: $PLATFORM"
    exit 1
fi

URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${TARBALL}"
DEST="$PROJECT_ROOT/third_party/onnxruntime"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/budo-onnx.XXXXXX")"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT
ARCHIVE="$TMP_DIR/$TARBALL.part"
STAGE="$TMP_DIR/stage"

echo "Downloading ORT ${ORT_VERSION} from ${URL}..."
curl --fail --location --retry 3 --connect-timeout 20 --max-time 900 \
    --output "$ARCHIVE" "$URL"
if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL_HASH="$(sha256sum "$ARCHIVE" | awk '{print $1}')"
else
    ACTUAL_HASH="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
fi
EXPECTED_HASH="${ORT_HASH#SHA256=}"
if [[ "$ACTUAL_HASH" != "$EXPECTED_HASH" ]]; then
    echo "ONNX Runtime archive SHA-256 mismatch" >&2
    echo "Expected: $EXPECTED_HASH" >&2
    echo "Actual:   $ACTUAL_HASH" >&2
    exit 1
fi

mkdir -p "$STAGE"
tar -xzf "$ARCHIVE" -C "$STAGE" --strip-components=1
rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
mv "$STAGE" "$DEST"
echo "ONNX Runtime installed to ${DEST}/"
