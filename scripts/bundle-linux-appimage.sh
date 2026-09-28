#!/usr/bin/env bash
# Build a Linux AppImage for Budo.
#
# Usage:
#   ./scripts/bundle-linux-appimage.sh
#
# The AppImage is written to build/Budo-<arch>.AppImage
#
# Requires:
#   - A built budo binary (run 'make build' first)
#   - curl or wget to provision the locked appimagetool into build/_tools/
#   - rsvg-convert (librsvg2-bin) or ImageMagick (`convert`) for icon rendering (optional)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

APP_NAME="Budo"
APP_ID="com.budo.app"
EXECUTABLE="budo"
BUILD_DIR="$PROJECT_ROOT/build"
APPDIR="$BUILD_DIR/${APP_NAME}.AppDir"
ICON_SVG="$PROJECT_ROOT/budo-icon.svg"
ARCH="$(uname -m)"
APPIMAGE_PATH="$BUILD_DIR/${APP_NAME}-${ARCH}.AppImage"

BINARY="$BUILD_DIR/$EXECUTABLE"
if [[ ! -f "$BINARY" ]]; then
    echo_error "Binary not found at $BINARY — run 'make build' first."
    exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
    echo_error "AppImage packaging is only supported on Linux."
    exit 1
fi

# ── Locate appimagetool ───────────────────────────────────────────────────────

APPIMAGETOOL_VERSION="$(budo_dependency_lock_value BUDO_APPIMAGETOOL_VERSION)"
APPIMAGETOOL_URL_TEMPLATE="$(budo_dependency_lock_value BUDO_APPIMAGETOOL_URL)"
case "$ARCH" in
    x86_64) APPIMAGETOOL_HASH="$(budo_dependency_lock_value BUDO_APPIMAGETOOL_X86_64_URL_HASH)" ;;
    aarch64) APPIMAGETOOL_HASH="$(budo_dependency_lock_value BUDO_APPIMAGETOOL_AARCH64_URL_HASH)" ;;
    *) echo_error "No locked appimagetool for Linux architecture: $ARCH"; exit 1 ;;
esac
EXPECTED_APPIMAGETOOL_SHA="${APPIMAGETOOL_HASH#SHA256=}"
APPIMAGETOOL_URL="${APPIMAGETOOL_URL_TEMPLATE//@ARCH@/$ARCH}"
TOOLS_DIR="$BUILD_DIR/_tools"
mkdir -p "$TOOLS_DIR"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-${APPIMAGETOOL_VERSION}-${ARCH}.AppImage"

sha256_file() {
    if command -v sha256sum &>/dev/null; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum &>/dev/null; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo_error "sha256sum or shasum is required to verify appimagetool."
        return 1
    fi
}

if [[ ! -f "$APPIMAGETOOL" ]] || [[ "$(sha256_file "$APPIMAGETOOL")" != "$EXPECTED_APPIMAGETOOL_SHA" ]]; then
    rm -f "$APPIMAGETOOL"
    APPIMAGETOOL_PART="$APPIMAGETOOL.part"
    rm -f "$APPIMAGETOOL_PART"
    echo_step "Downloading locked appimagetool $APPIMAGETOOL_VERSION from $APPIMAGETOOL_URL"
    if command -v curl &>/dev/null; then
        curl --fail --location --retry 3 --connect-timeout 20 --max-time 900 \
            --output "$APPIMAGETOOL_PART" "$APPIMAGETOOL_URL"
    elif command -v wget &>/dev/null; then
        wget --https-only --output-document="$APPIMAGETOOL_PART" "$APPIMAGETOOL_URL"
    else
        echo_error "Neither curl nor wget is available to download appimagetool."
        exit 1
    fi
    ACTUAL_APPIMAGETOOL_SHA="$(sha256_file "$APPIMAGETOOL_PART")"
    if [[ "$ACTUAL_APPIMAGETOOL_SHA" != "$EXPECTED_APPIMAGETOOL_SHA" ]]; then
        rm -f "$APPIMAGETOOL_PART"
        echo_error "appimagetool SHA-256 mismatch (expected $EXPECTED_APPIMAGETOOL_SHA, got $ACTUAL_APPIMAGETOOL_SHA)."
        exit 1
    fi
    mv "$APPIMAGETOOL_PART" "$APPIMAGETOOL"
fi

if [[ "$(sha256_file "$APPIMAGETOOL")" != "$EXPECTED_APPIMAGETOOL_SHA" ]]; then
    echo_error "Cached appimagetool failed SHA-256 verification."
    exit 1
fi
chmod 0755 "$APPIMAGETOOL"

# ── Stage AppDir ──────────────────────────────────────────────────────────────

echo_step "Staging AppDir at $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BINARY" "$APPDIR/usr/bin/$EXECUTABLE"
chmod +x "$APPDIR/usr/bin/$EXECUTABLE"

# ── Bundle non-system shared libraries ────────────────────────────────────────

echo_step "Resolving shared library dependencies…"
bundle_libs() {
    local bin="$1"
    local libs
    libs=$(ldd "$bin" 2>/dev/null | awk '{print $3}' | grep -E '^/' || true)
    for lib in $libs; do
        # Skip core system libs that should always come from the host distro.
        case "$(basename "$lib")" in
            ld-linux*|libc.so*|libdl.so*|libm.so*|libpthread.so*|librt.so*|\
            libresolv.so*|libutil.so*|libnsl.so*|libgcc_s.so*|libstdc++.so*|\
            libGL.so*|libGLX.so*|libGLdispatch.so*|libEGL.so*|libGLESv2.so*|\
            libX11.so*|libXext.so*|libXrandr.so*|libXi.so*|libXcursor.so*|\
            libXxf86vm.so*|libXfixes.so*|libxcb*.so*|libwayland*.so*|\
            libdrm.so*|libgbm.so*|libdbus-1.so*|libudev.so*|libsystemd.so*)
                continue
                ;;
        esac
        local dest="$APPDIR/usr/lib/$(basename "$lib")"
        if [[ ! -f "$dest" ]]; then
            cp -L "$lib" "$dest"
        fi
    done
}
bundle_libs "$APPDIR/usr/bin/$EXECUTABLE"

# Also bundle ONNX Runtime (versioned + symlink) when present.
ORT_LIB_DIR="$PROJECT_ROOT/third_party/onnxruntime/lib"
if [[ -d "$ORT_LIB_DIR" ]]; then
    shopt -s nullglob
    for f in "$ORT_LIB_DIR"/libonnxruntime*.so*; do
        cp -P "$f" "$APPDIR/usr/lib/"
    done
    shopt -u nullglob
fi

# ── Icons ─────────────────────────────────────────────────────────────────────

if [[ -f "$ICON_SVG" ]]; then
    cp "$ICON_SVG" "$APPDIR/usr/share/icons/hicolor/scalable/apps/${APP_ID}.svg"
    cp "$ICON_SVG" "$APPDIR/${APP_ID}.svg"

    PNG_OUT="$APPDIR/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png"
    if command -v rsvg-convert &>/dev/null; then
        rsvg-convert -w 256 -h 256 "$ICON_SVG" -o "$PNG_OUT"
    elif command -v convert &>/dev/null; then
        convert -background none -resize 256x256 "$ICON_SVG" "$PNG_OUT"
    else
        echo_warn "rsvg-convert / convert not found — skipping PNG icon."
    fi

    if [[ -f "$PNG_OUT" ]]; then
        cp "$PNG_OUT" "$APPDIR/${APP_ID}.png"
        # appimagetool also accepts a top-level .DirIcon
        cp "$PNG_OUT" "$APPDIR/.DirIcon"
    fi
else
    echo_warn "Icon SVG not found at $ICON_SVG — AppImage will have no custom icon."
fi

# ── Desktop entry ─────────────────────────────────────────────────────────────

DESKTOP_FILE="$APPDIR/${APP_ID}.desktop"
cat > "$DESKTOP_FILE" <<DESKTOP
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
cp "$DESKTOP_FILE" "$APPDIR/usr/share/applications/${APP_ID}.desktop"

# ── AppRun launcher ───────────────────────────────────────────────────────────

cat > "$APPDIR/AppRun" <<'APPRUN'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
exec "${HERE}/usr/bin/budo" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# ── Build the AppImage ────────────────────────────────────────────────────────

echo_step "Building AppImage…"
rm -f "$APPIMAGE_PATH"
ARCH="$ARCH" "$APPIMAGETOOL" --no-appstream "$APPDIR" "$APPIMAGE_PATH"

echo_info "AppImage created: $APPIMAGE_PATH"
stage_website_download "$APPIMAGE_PATH" "Budo-linux.AppImage"
