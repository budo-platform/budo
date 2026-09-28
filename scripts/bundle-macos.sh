#!/usr/bin/env bash
# Build a macOS .app bundle and optionally a .dmg disk image for Budo.
#
# Usage:
#   ./scripts/bundle-macos.sh [--dmg] [--sign IDENTITY]
#
# Options:
#   --dmg              Also create a .dmg disk image
#   --sign IDENTITY    Code-sign with the given identity (e.g. "Developer ID Application: …")
#
# The .app bundle is written to build/Budo.app
# The .dmg (if requested) is written to build/Budo.dmg

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

# ── Defaults ──────────────────────────────────────────────────────────────────

APP_NAME="Budo"
BUNDLE_ID="com.budo.app"
EXECUTABLE="budo"
BUILD_DIR="$PROJECT_ROOT/build"
APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
DMG_PATH="$BUILD_DIR/$APP_NAME.dmg"
ICON_SVG="$PROJECT_ROOT/budo-icon.svg"
CREATE_DMG=false
CODESIGN_IDENTITY=""

# ── Parse arguments ───────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dmg)
            CREATE_DMG=true
            shift
            ;;
        --sign)
            CODESIGN_IDENTITY="$2"
            shift 2
            ;;
        *)
            echo_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# ── Verify binary exists ─────────────────────────────────────────────────────

BINARY="$BUILD_DIR/$EXECUTABLE"
if [[ ! -f "$BINARY" ]]; then
    echo_error "Binary not found at $BINARY — run 'make build' first."
    exit 1
fi

# ── Generate .icns from SVG ──────────────────────────────────────────────────

generate_icns() {
    local icns_path="$1"
    local iconset_dir="$BUILD_DIR/$APP_NAME.iconset"

    if [[ ! -f "$ICON_SVG" ]]; then
        echo_warn "Icon SVG not found at $ICON_SVG — app bundle will have no custom icon."
        return 1
    fi

    # Prefer rsvg-convert (librsvg), fall back to sips via a temporary PNG
    if command -v rsvg-convert &>/dev/null; then
        local converter="rsvg-convert"
    elif command -v sips &>/dev/null; then
        local converter="sips"
    else
        echo_warn "Neither rsvg-convert nor sips found — skipping icon generation."
        return 1
    fi

    echo_step "Generating .icns icon…"
    rm -rf "$iconset_dir"
    mkdir -p "$iconset_dir"

    local sizes=(16 32 64 128 256 512)
    for size in "${sizes[@]}"; do
        local retina=$((size * 2))
        if [[ "$converter" == "rsvg-convert" ]]; then
            rsvg-convert -w "$size"   -h "$size"   "$ICON_SVG" -o "$iconset_dir/icon_${size}x${size}.png"
            rsvg-convert -w "$retina" -h "$retina" "$ICON_SVG" -o "$iconset_dir/icon_${size}x${size}@2x.png"
        else
            # sips can't read SVG directly — render a large master PNG first
            local master="$BUILD_DIR/_icon_master.png"
            if [[ ! -f "$master" ]]; then
                # Use qlmanage to render SVG to PNG (available on macOS)
                qlmanage -t -s 1024 -o "$BUILD_DIR" "$ICON_SVG" &>/dev/null || true
                local ql_out="$BUILD_DIR/$(basename "$ICON_SVG").png"
                if [[ -f "$ql_out" ]]; then
                    mv "$ql_out" "$master"
                else
                    echo_warn "Could not render SVG to PNG — skipping icon."
                    rm -rf "$iconset_dir"
                    return 1
                fi
            fi
            sips -z "$size"   "$size"   "$master" --out "$iconset_dir/icon_${size}x${size}.png"    &>/dev/null
            sips -z "$retina" "$retina" "$master" --out "$iconset_dir/icon_${size}x${size}@2x.png" &>/dev/null
        fi
    done

    iconutil -c icns -o "$icns_path" "$iconset_dir"
    rm -rf "$iconset_dir" "$BUILD_DIR/_icon_master.png"
    echo_info "Icon created: $icns_path"
}

# ── Create .app bundle ───────────────────────────────────────────────────────

echo_step "Creating $APP_NAME.app bundle…"

rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"

# Copy binary
cp "$BINARY" "$APP_BUNDLE/Contents/MacOS/$EXECUTABLE"
chmod +x "$APP_BUNDLE/Contents/MacOS/$EXECUTABLE"

# Copy any bundled dylibs next to the binary and rewrite rpaths
copy_dylibs() {
    local bin="$APP_BUNDLE/Contents/MacOS/$EXECUTABLE"
    local frameworks_dir="$APP_BUNDLE/Contents/Frameworks"

    # Find non-system dylibs
    local dylibs
    dylibs=$(otool -L "$bin" 2>/dev/null | awk 'NR>1{print $1}' | grep -v '^/usr/lib' | grep -v '^/System' | grep -v '@rpath' || true)

    # Also handle @rpath references by checking the build directory
    local rpath_libs
    rpath_libs=$(otool -L "$bin" 2>/dev/null | awk 'NR>1{print $1}' | grep '@rpath' || true)

    local has_libs=false
    for lib in $dylibs $rpath_libs; do
        local resolved="$lib"
        if [[ "$lib" == @rpath/* ]]; then
            local libname="${lib#@rpath/}"
            # Search common locations
            for search_dir in "$BUILD_DIR" "$PROJECT_ROOT/third_party/onnxruntime/lib" "/usr/local/lib"; do
                if [[ -f "$search_dir/$libname" ]]; then
                    resolved="$search_dir/$libname"
                    break
                fi
            done
            if [[ "$resolved" == "$lib" ]]; then
                continue  # Could not resolve; skip
            fi
        fi

        if [[ -f "$resolved" ]]; then
            has_libs=true
            mkdir -p "$frameworks_dir"
            cp "$resolved" "$frameworks_dir/"
            local basename
            basename=$(basename "$resolved")
            install_name_tool -change "$lib" "@executable_path/../Frameworks/$basename" "$bin" 2>/dev/null || true
            echo_info "Bundled dylib: $basename"
        fi
    done

    if $has_libs; then
        install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin" 2>/dev/null || true
    fi
}
copy_dylibs

# Generate icon
ICNS_PATH="$APP_BUNDLE/Contents/Resources/$APP_NAME.icns"
generate_icns "$ICNS_PATH" || true

# Determine version: prefer git tag (allows release tags to override),
# otherwise fall back to BUDO_VERSION_STRING in src/core/version.h.
VERSION=$(git -C "$PROJECT_ROOT" describe --tags --abbrev=0 2>/dev/null || true)
if [ -z "$VERSION" ]; then
    VERSION=$(grep -E '^#define BUDO_VERSION_STRING' \
        "$PROJECT_ROOT/src/core/version.h" 2>/dev/null \
        | sed -E 's/.*"([^"]+)".*/\1/')
fi
VERSION="${VERSION:-0.0.0}"
VERSION="${VERSION#v}"  # strip leading 'v'
BUILD_NUMBER=$(git -C "$PROJECT_ROOT" rev-list --count HEAD 2>/dev/null || echo "1")

# Write Info.plist
cat > "$APP_BUNDLE/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleVersion</key>
    <string>${BUILD_NUMBER}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>${EXECUTABLE}</string>
    <key>CFBundleIconFile</key>
    <string>${APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
</dict>
</plist>
PLIST

echo_info "Created $APP_BUNDLE"

# ── Code signing ──────────────────────────────────────────────────────────────

if [[ -n "$CODESIGN_IDENTITY" ]]; then
    echo_step "Code-signing with identity: $CODESIGN_IDENTITY"
    codesign --force --deep --sign "$CODESIGN_IDENTITY" "$APP_BUNDLE"
    echo_info "Code-signed $APP_BUNDLE"
elif command -v codesign &>/dev/null; then
    echo_step "Ad-hoc code-signing…"
    codesign --force --deep --sign - "$APP_BUNDLE"
    echo_info "Ad-hoc signed $APP_BUNDLE"
fi

# ── Create DMG ────────────────────────────────────────────────────────────────

if $CREATE_DMG; then
    echo_step "Creating DMG…"

    rm -f "$DMG_PATH"

    # Create a temporary directory for DMG contents
    DMG_STAGING="$BUILD_DIR/_dmg_staging"
    rm -rf "$DMG_STAGING"
    mkdir -p "$DMG_STAGING"

    # Copy .app bundle
    cp -R "$APP_BUNDLE" "$DMG_STAGING/"

    # Create a symbolic link to /Applications for drag-to-install
    ln -s /Applications "$DMG_STAGING/Applications"

    # Create the DMG
    hdiutil create \
        -volname "$APP_NAME" \
        -srcfolder "$DMG_STAGING" \
        -ov \
        -format UDZO \
        "$DMG_PATH"

    rm -rf "$DMG_STAGING"
    echo_info "Created $DMG_PATH"
    stage_website_download "$DMG_PATH" "Budo.dmg"
fi

echo ""
echo_info "Done! App bundle: $APP_BUNDLE"
if $CREATE_DMG; then
    echo_info "DMG image:  $DMG_PATH"
fi
