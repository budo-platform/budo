#!/usr/bin/env bash
# Download and build Skia from source for WebAssembly (Emscripten).
#
# Prerequisites:
#   - git, python3, ninja
#   - project-local Emscripten SDK (downloaded automatically into third_party/emsdk)
#
# Output: third_party/skia-wasm/out/wasm/libskia.a

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

SKIA_WASM_DIR="$PROJECT_ROOT/third_party/skia-wasm"
BUILD_DIR="$SKIA_WASM_DIR/out/wasm"
SKIA_REPOSITORY="$(budo_dependency_lock_value BUDO_SKIA_GIT_REPOSITORY)"
SKIA_COMMIT="$(budo_dependency_lock_value BUDO_SKIA_WEB_GIT_TAG)"

echo_step "Skia WASM Setup Script"

# ── Check requirements ────────────────────────────────────────────────────────

check_requirements() {
    echo_info "Checking requirements..."

    require_cmd git
    require_cmd python3

    if ! command -v ninja &>/dev/null; then
        echo_warn "ninja is not installed — attempting to install..."
        if [[ "$OSTYPE" == "darwin"* ]]; then
            brew install ninja
        elif [[ -f /etc/debian_version ]]; then
            sudo apt-get install -y ninja-build
        else
            echo_error "Please install ninja manually"
            exit 1
        fi
    fi

    activate_vendored_emscripten

    echo_info "Using Emscripten SDK: $EMSDK"
    echo_info "emcc version: $(emcc --version | head -1)"
    echo_info "Requirements OK"
}

# ── Clone or update Skia ──────────────────────────────────────────────────────

clone_skia() {
    if [ -d "$SKIA_WASM_DIR/.git" ]; then
        echo_info "Skia already cloned; selecting locked commit ${SKIA_COMMIT:0:12}..."
    else
        echo_info "Cloning Skia..."
        mkdir -p "$(dirname "$SKIA_WASM_DIR")"
        git clone --no-checkout "$SKIA_REPOSITORY" "$SKIA_WASM_DIR"
    fi
    cd "$SKIA_WASM_DIR"
    git fetch origin "$SKIA_COMMIT"
    git checkout --detach "$SKIA_COMMIT"
}

# ── Sync dependencies ─────────────────────────────────────────────────────────

sync_deps() {
    echo_info "Syncing Skia dependencies..."
    cd "$SKIA_WASM_DIR"
    python3 tools/git-sync-deps
}

# ── Build Skia for WASM ──────────────────────────────────────────────────────

build_skia() {
    echo_step "Building Skia for WebAssembly..."
    cd "$SKIA_WASM_DIR"

    bin/gn gen "$BUILD_DIR" --args="
        target_cpu=\"wasm\"
        is_official_build=true
        is_component_build=false

        skia_emsdk_dir=\"${EMSDK}\"

        skia_use_webgl=true
        skia_gl_standard=\"webgl\"
        skia_enable_ganesh=true
        skia_use_gl=true

        skia_use_freetype=true
        skia_use_harfbuzz=false
        skia_use_system_freetype2=false
        skia_use_system_libjpeg_turbo=false
        skia_use_system_libpng=false
        skia_use_system_libwebp=false
        skia_use_system_zlib=false
        skia_use_system_expat=false
        skia_use_system_icu=false

        skia_use_expat=true
        skia_use_wuffs=true

        skia_enable_skshaper=true
        skia_enable_skparagraph=false
        skia_enable_pdf=false
        skia_enable_skottie=false
        skia_enable_svg=true
        skia_enable_tools=false

        skia_use_vulkan=false
        skia_use_metal=false
        skia_use_dawn=false
    "

    ninja -C "$BUILD_DIR" skia modules/svg

    echo_info "Built Skia WASM -> $BUILD_DIR/libskia.a"
}

# ── Help ──────────────────────────────────────────────────────────────────────

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Setup and build Skia for WebAssembly (Emscripten)."
    echo ""
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  --clean        Clean Skia build directory before building"
    echo ""
    echo "Prerequisites:"
    echo "  - project-local Emscripten SDK (downloaded automatically into third_party/emsdk)"
    echo "  - git, python3, ninja"
    echo ""
    echo "Output:"
    echo "  third_party/skia-wasm/out/wasm/libskia.a"
    echo ""
    echo "After building, use:"
    echo "  make web-build WEB_APP=examples/demo"
}

# ── Parse arguments ───────────────────────────────────────────────────────────

CLEAN=false

for arg in "$@"; do
    case "$arg" in
        -h|--help)
            show_help
            exit 0
            ;;
        --clean)
            CLEAN=true
            ;;
        *)
            echo_error "Unknown argument: $arg"
            show_help
            exit 1
            ;;
    esac
done

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
    check_requirements

    if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
        echo_info "Cleaning previous WASM build..."
        rm -rf "$BUILD_DIR"
    fi

    clone_skia
    sync_deps
    build_skia

    echo ""
    echo_info "Skia WASM built successfully!"
    echo ""
    echo "To build the web target with Skia:"
    echo ""
    echo "  make web-build WEB_APP=examples/demo"
    echo ""
}

main "$@"
