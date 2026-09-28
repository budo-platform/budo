#!/usr/bin/env bash
# Build a Linux Flatpak bundle for Budo.
#
# Usage:
#   ./scripts/bundle-linux-flatpak.sh [--install] [--no-bundle]
#
# Options:
#   --install     After building, install the resulting .flatpak into the user
#                 installation (`flatpak install --user ...`).
#   --no-bundle   Build the repo but skip producing a single-file .flatpak bundle.
#
# Outputs:
#   build/flatpak/repo/         OSTree repo with the built app
#   build/Budo.flatpak      Single-file bundle (default)
#   build/budo-flatpak      Host-side CLI wrapper for `budo ...`
#
# Requires:
#   - A built budo binary (run 'make build' first)
#   - flatpak and flatpak-builder in PATH
#   - The org.freedesktop.{Platform,Sdk}//24.08 runtime + SDK
#     (the script will offer to install them from Flathub)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

APP_ID="com.budo.app"
APP_NAME="Budo"
EXECUTABLE="budo"
RUNTIME_VERSION="24.08"

BUILD_DIR="$PROJECT_ROOT/build"
FLATPAK_DIR="$BUILD_DIR/flatpak"
PAYLOAD_DIR="$FLATPAK_DIR/payload"
BUILD_STATE="$FLATPAK_DIR/build-dir"
REPO_DIR="$FLATPAK_DIR/repo"
BUNDLE_PATH="$BUILD_DIR/${APP_NAME}.flatpak"
CLI_WRAPPER_PATH="$BUILD_DIR/${EXECUTABLE}-flatpak"
MANIFEST_SRC="$PROJECT_ROOT/packaging/flatpak/${APP_ID}.yml"
MANIFEST_DST="$FLATPAK_DIR/${APP_ID}.yml"
ICON_SVG="$PROJECT_ROOT/budo-icon.svg"

INSTALL=false
MAKE_BUNDLE=true
while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)    INSTALL=true; shift ;;
        --no-bundle)  MAKE_BUNDLE=false; shift ;;
        *) echo_error "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo_error "Flatpak packaging is only supported on Linux."
    exit 1
fi

BINARY="$BUILD_DIR/$EXECUTABLE"
if [[ ! -f "$BINARY" ]]; then
    echo_error "Binary not found at $BINARY — run 'make build' first."
    exit 1
fi

if ! command -v flatpak &>/dev/null || ! command -v flatpak-builder &>/dev/null; then
    echo_error "flatpak and flatpak-builder are required."
    echo_error "Install them with your distro's package manager, e.g.:"
    echo_error "  sudo apt install flatpak flatpak-builder"
    exit 1
fi

if [[ ! -f "$MANIFEST_SRC" ]]; then
    echo_error "Flatpak manifest not found at $MANIFEST_SRC"
    exit 1
fi

# ── Ensure Flathub remote + runtime/SDK are installed ────────────────────────

if ! flatpak --user remotes 2>/dev/null | awk '{print $1}' | grep -qx flathub; then
    echo_step "Adding Flathub remote (user)…"
    flatpak --user remote-add --if-not-exists flathub \
        https://flathub.org/repo/flathub.flatpakrepo
fi

ensure_runtime() {
    local ref="$1"
    if ! flatpak --user info "$ref" &>/dev/null \
       && ! flatpak info "$ref" &>/dev/null; then
        echo_step "Installing $ref from Flathub…"
        flatpak --user install -y flathub "$ref"
    fi
}
ensure_runtime "org.freedesktop.Platform//${RUNTIME_VERSION}"
ensure_runtime "org.freedesktop.Sdk//${RUNTIME_VERSION}"

# ── Host CLI wrapper ─────────────────────────────────────────────────────────

write_cli_wrapper() {
    cat > "$CLI_WRAPPER_PATH" <<'WRAPPER'
#!/usr/bin/env bash
# BUDO_FLATPAK_WRAPPER
# Host-side command shim for the Flatpak build. Flatpak does not export
# /app/bin/budo into the user's shell PATH, so this script provides the
# expected `budo ...` command while preserving cwd-relative project paths.

set -euo pipefail

APP_ID="${BUDO_FLATPAK_APP_ID:-com.budo.app}"

canonical_existing_dir() {
    local path="$1"
    if [[ -d "$path" ]]; then
        (cd "$path" && pwd -P)
    else
        local parent
        parent="$(dirname "$path")"
        (cd "$parent" 2>/dev/null && pwd -P) || return 1
    fi
}

absolute_path() {
    local path="$1"
    case "$path" in
        /*) printf '%s\n' "$path" ;;
        *)  printf '%s/%s\n' "$HOST_CWD" "$path" ;;
    esac
}

add_filesystem_override() {
    local path="$1"
    [[ -z "$path" || "$path" == -* ]] && return 0

    local abs grant
    abs="$(absolute_path "$path")"
    if [[ -e "$abs" ]]; then
        if command -v realpath >/dev/null 2>&1; then
            grant="$(realpath "$abs")"
        else
            grant="$(canonical_existing_dir "$abs")"
            [[ -f "$abs" ]] && grant="$grant/$(basename "$abs")"
        fi
    else
        grant="$(canonical_existing_dir "$abs")" || return 0
    fi

    FLATPAK_OVERRIDES+=("--filesystem=$grant")
}

HOST_CWD="$(pwd -P 2>/dev/null || pwd)"
ORIGINAL_ARGS=("$@")
FLATPAK_OVERRIDES=("--cwd=$HOST_CWD" "--filesystem=$HOST_CWD")

if [[ $# -gt 0 ]]; then
    case "$1" in
        run|init|web-serve|web-export|android-apk|android-aab)
            [[ $# -gt 1 ]] && add_filesystem_override "$2"
            ;;
        help|--help|-h|budo.d.ts|budo-llm.md)
            ;;
        *)
            add_filesystem_override "$1"
            ;;
    esac
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --file-root|-o|--output)
            shift
            [[ $# -gt 0 ]] && add_filesystem_override "$1"
            ;;
    esac
    shift || true
done

exec flatpak run "${FLATPAK_OVERRIDES[@]}" "$APP_ID" "${ORIGINAL_ARGS[@]}"
WRAPPER
    chmod +x "$CLI_WRAPPER_PATH"
}

write_cli_wrapper
echo_info "CLI wrapper created: $CLI_WRAPPER_PATH"

# ── Stage payload ─────────────────────────────────────────────────────────────

echo_step "Staging payload at $PAYLOAD_DIR"
rm -rf "$FLATPAK_DIR"
mkdir -p "$PAYLOAD_DIR/usr/bin" "$PAYLOAD_DIR/usr/lib" \
         "$PAYLOAD_DIR/usr/share/icons/hicolor/scalable/apps" \
         "$PAYLOAD_DIR/usr/share/icons/hicolor/256x256/apps"

cp "$BINARY" "$PAYLOAD_DIR/usr/bin/$EXECUTABLE"
chmod +x "$PAYLOAD_DIR/usr/bin/$EXECUTABLE"

# Bundle non-system shared libraries (mirrors AppImage logic).
SYSTEM_LIB_RE='^(ld-linux|libc|libdl|libm|libpthread|librt|libresolv|libutil|libnsl|libgcc_s|libstdc\+\+|libGL|libGLX|libGLdispatch|libEGL|libGLES|libX|libxcb|libwayland|libdrm|libgbm|libdbus-1|libudev|libsystemd|libfreetype|libfontconfig|libssl|libcrypto|libasound|libpulse|libSDL2)\.so'
ldd "$PAYLOAD_DIR/usr/bin/$EXECUTABLE" 2>/dev/null \
  | awk '{print $3}' | grep -E '^/' \
  | while read -r lib; do
        base="$(basename "$lib")"
        if [[ "$base" =~ $SYSTEM_LIB_RE ]]; then continue; fi
        cp -L "$lib" "$PAYLOAD_DIR/usr/lib/" 2>/dev/null || true
    done

# Bundle ONNX Runtime libs when present.
ORT_LIB_DIR="$PROJECT_ROOT/third_party/onnxruntime/lib"
if [[ -d "$ORT_LIB_DIR" ]]; then
    shopt -s nullglob
    for f in "$ORT_LIB_DIR"/libonnxruntime*.so*; do
        cp -P "$f" "$PAYLOAD_DIR/usr/lib/"
    done
    shopt -u nullglob
fi

# Icons.
if [[ -f "$ICON_SVG" ]]; then
    cp "$ICON_SVG" "$PAYLOAD_DIR/usr/share/icons/hicolor/scalable/apps/${APP_ID}.svg"
    PNG_OUT="$PAYLOAD_DIR/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png"
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w 256 -h 256 "$ICON_SVG" -o "$PNG_OUT"
    elif command -v convert &>/dev/null; then
        convert -background none -resize 256x256 "$ICON_SVG" "$PNG_OUT"
    else
        echo_warn "rsvg-convert / convert not found — skipping PNG icon."
    fi
else
    echo_warn "Icon SVG not found at $ICON_SVG — Flatpak will have no custom icon."
fi

# Desktop entry.
cat > "$PAYLOAD_DIR/${APP_ID}.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=${APP_NAME}
GenericName=Budo Runtime
Comment=Lightweight cross-platform runtime for animated 2D apps
Exec=${EXECUTABLE} %F
Icon=${APP_ID}
Terminal=false
Categories=Development;Graphics;
DESKTOP

# Stage the manifest next to the payload directory it references.
cp "$MANIFEST_SRC" "$MANIFEST_DST"

# ── Build via flatpak-builder ────────────────────────────────────────────────

echo_step "Running flatpak-builder…"
( cd "$FLATPAK_DIR" && \
  flatpak-builder --user --force-clean \
    --repo="$REPO_DIR" \
    "$BUILD_STATE" \
    "$(basename "$MANIFEST_DST")" )

# ── Export single-file bundle ────────────────────────────────────────────────

if $MAKE_BUNDLE; then
    echo_step "Creating bundle $BUNDLE_PATH"
    rm -f "$BUNDLE_PATH"
    flatpak build-bundle "$REPO_DIR" "$BUNDLE_PATH" "$APP_ID"
    echo_info "Flatpak bundle created: $BUNDLE_PATH"
    stage_website_download "$BUNDLE_PATH" "Budo.flatpak"
    stage_website_download "$CLI_WRAPPER_PATH" "budo-linux-flatpak"
    echo_info "Install with:  flatpak --user install --bundle $BUNDLE_PATH"
    echo_info "Install the shell command with:  install -Dm755 $CLI_WRAPPER_PATH ~/.local/bin/budo"
fi

if $INSTALL; then
    echo_step "Installing into user Flatpak installation…"
    if $MAKE_BUNDLE; then
        flatpak --user install -y --reinstall --bundle "$BUNDLE_PATH"
    else
        flatpak --user install -y --reinstall "$REPO_DIR" "$APP_ID"
    fi
    CLI_TARGET="$HOME/.local/bin/$EXECUTABLE"
    if [[ -e "$CLI_TARGET" ]] && ! grep -Fq "BUDO_FLATPAK_WRAPPER" "$CLI_TARGET" 2>/dev/null; then
        echo_warn "Not overwriting existing command at $CLI_TARGET"
        echo_warn "Flatpak wrapper remains available at $CLI_WRAPPER_PATH"
    else
        install -Dm755 "$CLI_WRAPPER_PATH" "$CLI_TARGET"
        echo_info "Shell command installed: $CLI_TARGET"
        case ":$PATH:" in
            *":$HOME/.local/bin:"*) ;;
            *) echo_warn "$HOME/.local/bin is not in PATH; add it to your shell profile to use '$EXECUTABLE'." ;;
        esac
    fi
    echo_info "Run with:  $EXECUTABLE run /path/to/app"
    echo_info "Direct Flatpak run remains available:  flatpak run $APP_ID /path/to/app"
fi
