#!/usr/bin/env bash
# Download and activate the project-local Emscripten SDK.
#
# This script vendors emsdk under third_party/emsdk and downloads/activates the
# requested Emscripten toolchain inside that directory only. It does not require
# or modify a system-wide Emscripten installation.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -L)"
source "$SCRIPT_DIR/_common.sh"

EMSDK_VERSION="${BUDO_EMSDK_VERSION:-${EMSDK_VERSION:-$BUDO_DEFAULT_EMSDK_VERSION}}"
EMSDK_DIR="${BUDO_EMSDK_DIR:-${EMSDK_DIR:-$PROJECT_ROOT/third_party/emsdk}}"
EMSDK_REPO="${BUDO_EMSDK_REPO:-$(budo_dependency_lock_value BUDO_EMSDK_GIT_REPOSITORY)}"
EMSDK_COMMIT="$(budo_dependency_lock_value BUDO_EMSDK_GIT_TAG)"

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Download and prepare the project-local Emscripten SDK."
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message"
    echo "  --version VERSION    Emscripten SDK version (default: $BUDO_DEFAULT_EMSDK_VERSION)"
    echo "  --dir DIR            emsdk checkout directory (default: third_party/emsdk)"
    echo ""
    echo "Environment overrides:"
    echo "  BUDO_EMSDK_VERSION   Emscripten SDK version"
    echo "  BUDO_EMSDK_DIR       emsdk checkout directory"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        --version)
            EMSDK_VERSION="$2"
            shift 2
            ;;
        --dir)
            EMSDK_DIR="$2"
            shift 2
            ;;
        *)
            echo_error "Unknown argument: $1"
            show_help
            exit 1
            ;;
    esac
done

if [[ ! "$EMSDK_DIR" = /* ]]; then
    EMSDK_DIR="$PROJECT_ROOT/$EMSDK_DIR"
fi

require_cmd git
require_cmd python3

echo_step "Project-local Emscripten SDK"
echo_info "Version: $EMSDK_VERSION"
echo_info "Directory: $EMSDK_DIR"

if [ -d "$EMSDK_DIR/.git" ]; then
    echo_info "emsdk already exists; selecting locked metadata commit ${EMSDK_COMMIT:0:12}..."
else
    echo_info "Cloning emsdk..."
    mkdir -p "$(dirname "$EMSDK_DIR")"
    git clone --no-checkout "$EMSDK_REPO" "$EMSDK_DIR"
fi

git -C "$EMSDK_DIR" fetch origin "$EMSDK_COMMIT"
git -C "$EMSDK_DIR" checkout --detach "$EMSDK_COMMIT"

cd "$EMSDK_DIR"

echo_info "Downloading Emscripten $EMSDK_VERSION into $EMSDK_DIR..."
./emsdk install "$EMSDK_VERSION"

echo_info "Activating Emscripten $EMSDK_VERSION locally..."
if ./emsdk activate --embedded "$EMSDK_VERSION" >/dev/null 2>&1; then
    ./emsdk activate --embedded "$EMSDK_VERSION"
else
    ./emsdk activate "$EMSDK_VERSION"
fi

find "$EMSDK_DIR/upstream/emscripten" -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null || true

# Validate by sourcing the generated environment in this process.
export EMSDK_QUIET=1
export EM_CONFIG="$EMSDK_DIR/.emscripten"
export EM_CACHE="$EMSDK_DIR/upstream/emscripten/cache"

# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" >/dev/null

export EM_CONFIG="$EMSDK_DIR/.emscripten"
export EM_CACHE="$EMSDK_DIR/upstream/emscripten/cache"

require_cmd emcc "project-local Emscripten (emcc)"
require_cmd emcmake "project-local Emscripten (emcmake)"
require_cmd emmake "project-local Emscripten (emmake)"

echo_info "emcc: $(emcc --version | head -1)"
printf '%s\n' "$EMSDK_VERSION" > "$EMSDK_DIR/.budo-emsdk-version"
echo_info "Emscripten SDK ready."
