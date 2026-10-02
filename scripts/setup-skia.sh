#!/usr/bin/env bash
# Download and build Skia from source for desktop.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

SKIA_DIR="$PROJECT_ROOT/third_party/skia"
BUILD_DIR="$SKIA_DIR/out/Release"
SKIA_REPOSITORY="$(budo_dependency_lock_value BUDO_SKIA_GIT_REPOSITORY)"
SKIA_COMMIT="$(budo_dependency_lock_value BUDO_SKIA_DESKTOP_GIT_TAG)"

echo_step "Skia Setup Script"

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

    echo_info "Requirements OK"
}

# ── Clone or update Skia ──────────────────────────────────────────────────────

clone_skia() {
    if [ -d "$SKIA_DIR/.git" ]; then
        echo_info "Skia already cloned; selecting locked commit ${SKIA_COMMIT:0:12}..."
    else
        echo_info "Cloning Skia..."
        mkdir -p "$(dirname "$SKIA_DIR")"
        git clone --no-checkout "$SKIA_REPOSITORY" "$SKIA_DIR"
    fi
    cd "$SKIA_DIR"
    git fetch origin "$SKIA_COMMIT"
    git checkout --detach "$SKIA_COMMIT"
}

# ── Sync dependencies ─────────────────────────────────────────────────────────

sync_deps() {
    echo_info "Syncing Skia dependencies..."
    cd "$SKIA_DIR"
    # Dozens of parallel fetches from googlesource.com: one transient failure
    # aborts the whole sync, so retry (already-synced deps are kept).
    local attempt
    for attempt in 1 2 3; do
        if python3 tools/git-sync-deps; then
            return 0
        fi
        if [ "$attempt" -lt 3 ]; then
            echo_warn "Dependency sync failed (attempt $attempt/3); retrying in $((attempt * 20)) s..."
            sleep $((attempt * 20))
        fi
    done
    echo_error "Skia dependency sync failed after 3 attempts"
    exit 1
}

# ── Build Skia ────────────────────────────────────────────────────────────────

build_skia() {
    echo_step "Building Skia..."
    cd "$SKIA_DIR"

    # Enable Ganesh GPU + GL backend on every desktop platform so Skia can
    # render straight into a GL FBO. On macOS we additionally keep Metal
    # available; Skia's Ganesh GL backend coexists with Metal in the same build.
    local extra_args="skia_use_gl=true"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        extra_args="${extra_args} skia_use_metal=true"
    fi

    bin/gn gen "$BUILD_DIR" --args="
        is_official_build=true
        is_component_build=false
        skia_use_system_expat=false
        skia_use_system_libjpeg_turbo=false
        skia_use_system_libpng=false
        skia_use_system_libwebp=false
        skia_use_system_zlib=false
        skia_use_system_icu=false
        skia_enable_pdf=false
        skia_enable_skottie=false
        skia_enable_skshaper=false
        ${extra_args}
    "

    ninja -C "$BUILD_DIR"
}

# ── Main ──────────────────────────────────────────────────────────────────────

main() {
    check_requirements
    clone_skia
    sync_deps
    build_skia

    echo ""
    echo_info "Skia built successfully!"
    echo ""
    echo "To build budo, run:"
    echo ""
    echo "  cmake -B build -DSKIA_PATH=$SKIA_DIR"
    echo "  cmake --build build"
    echo ""
    echo "Or using make:"
    echo ""
    echo "  make CMAKE_FLAGS=\"-DSKIA_PATH=$SKIA_DIR\""
    echo ""
}

main "$@"
