#!/usr/bin/env bash
# ============================================================================
# Budo — Web Export Script
# ============================================================================
#
# Builds a self-contained static web folder from a Budo project.
#
# Usage:
#   ./scripts/build-web.sh <project_dir> [-o output_dir]
#   ./scripts/build-web.sh examples/demo
#   ./scripts/build-web.sh examples/demo -o dist/web
#
# The output folder contains index.html, budo.js, budo.wasm,
# and budo.data — a fully deployable static site.
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

# ── Defaults ──────────────────────────────────────────────────────────────────

APP_FOLDER=""
OUTPUT_DIR=""
WEB_BUILD_DIR="$PROJECT_ROOT/build-web"
QUICKJS_IMPL="${QUICKJS_IMPL:-$(budo_dependency_lock_value BUDO_QUICKJS_IMPL_DEFAULT)}"

# ── Help ──────────────────────────────────────────────────────────────────────

show_help() {
    echo "Usage: $0 [OPTIONS] <project_dir>"
    echo ""
    echo "Build a self-contained web export from a Budo project."
    echo ""
    echo "Arguments:"
    echo "  project_dir    Path to the project folder (must contain main.js, main.lua, main.wat, or main.wasm)"
    echo ""
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -o, --output   Output directory (default: dist/web)"
    echo "  QUICKJS_IMPL   Environment variable: bellard or ng (default: $QUICKJS_IMPL)"
    echo ""
    echo "Examples:"
    echo "  $0 examples/demo"
    echo "  $0 examples/hello_world -o dist/web"
    echo "  $0 --output /tmp/export examples/shader_demo"
}

# ── Parse arguments ───────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        *)
            if [ -z "$APP_FOLDER" ]; then
                APP_FOLDER="$1"
            else
                echo_error "Unknown argument: $1"
                show_help
                exit 1
            fi
            shift
            ;;
    esac
done

# ── Validate ──────────────────────────────────────────────────────────────────

if [ -z "$APP_FOLDER" ]; then
    echo_error "Project directory is required"
    show_help
    exit 1
fi

# Make path absolute if relative
if [[ ! "$APP_FOLDER" = /* ]]; then
    APP_FOLDER="$PROJECT_ROOT/$APP_FOLDER"
fi

if [ ! -d "$APP_FOLDER" ]; then
    echo_error "Project directory does not exist: $APP_FOLDER"
    exit 1
fi

if [ ! -f "$APP_FOLDER/main.js" ] && [ ! -f "$APP_FOLDER/main.lua" ] && \
   [ ! -f "$APP_FOLDER/main.wat" ] && [ ! -f "$APP_FOLDER/main.wasm" ]; then
    echo_error "Project directory must contain main.js, main.lua, main.wat, or main.wasm: $APP_FOLDER"
    exit 1
fi

# Default output directory
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$PROJECT_ROOT/dist/web"
fi
# Make output path absolute if relative
if [[ ! "$OUTPUT_DIR" = /* ]]; then
    OUTPUT_DIR="$PROJECT_ROOT/$OUTPUT_DIR"
fi

# Use the project-local Emscripten SDK.
activate_vendored_emscripten
reset_web_build_if_emscripten_changed "$WEB_BUILD_DIR"

# ── Read app metadata ─────────────────────────────────────────────────────────

APP_JSON="$APP_FOLDER/app.json"

if [ -f "$APP_JSON" ]; then
    APP_NAME="$(json_get "$APP_JSON" name "")"
    APP_AUTHOR="$(json_get "$APP_JSON" author "")"
    APP_VERSION="$(json_get "$APP_JSON" version "")"
    if [ -z "$APP_VERSION" ]; then
        APP_VERSION="$(json_get "$APP_JSON" version_name "")"
    fi
    APP_DESCRIPTION="$(json_get_nested "$APP_JSON" store_listing.short_description "")"
    APP_ICON="$(json_get "$APP_JSON" icon "")"
else
    APP_NAME=""
    APP_AUTHOR=""
    APP_VERSION=""
    APP_DESCRIPTION=""
    APP_ICON=""
fi

# Fall back to directory basename for name
if [ -z "$APP_NAME" ]; then
    APP_NAME="$(basename "$APP_FOLDER")"
fi

echo_info "Exporting: $APP_NAME"
[ -n "$APP_AUTHOR" ]  && echo_info "  Author:  $APP_AUTHOR"
[ -n "$APP_VERSION" ] && echo_info "  Version: $APP_VERSION"

# ── Step 1: Build the WASM runtime ───────────────────────────────────────────

echo_step "Building WASM runtime with project assets..."

mkdir -p "$WEB_BUILD_DIR"

emcmake cmake -B "$WEB_BUILD_DIR" -S "$PROJECT_ROOT/web" \
    -DBUDO_ROOT="$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUDO_QUICKJS_IMPL="$QUICKJS_IMPL" \
    -DPROJECT_ASSET_DIR="$APP_FOLDER"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
emmake cmake --build "$WEB_BUILD_DIR" -j"$NPROC"

# ── Step 2: Assemble output directory ────────────────────────────────────────

echo_step "Assembling output in $OUTPUT_DIR..."

mkdir -p "$OUTPUT_DIR"

# Copy runtime artifacts
cp "$WEB_BUILD_DIR/budo.js"   "$OUTPUT_DIR/budo.js"
cp "$WEB_BUILD_DIR/budo.wasm" "$OUTPUT_DIR/budo.wasm"

# Copy preloaded data file (contains project assets)
if [ -f "$WEB_BUILD_DIR/budo.data" ]; then
    cp "$WEB_BUILD_DIR/budo.data" "$OUTPUT_DIR/budo.data"
fi

# ── Step 3: Copy favicon if app icon exists ──────────────────────────────────

if [ -n "$APP_ICON" ] && [ -f "$APP_FOLDER/$APP_ICON" ]; then
    ICON_EXT="${APP_ICON##*.}"
    cp "$APP_FOLDER/$APP_ICON" "$OUTPUT_DIR/favicon.$ICON_EXT"
    FAVICON_FILE="favicon.$ICON_EXT"
    echo_info "  Icon:    $APP_ICON → $FAVICON_FILE"
else
    FAVICON_FILE=""
fi

# ── Step 4: Generate index.html from template ───────────────────────────────

echo_step "Generating index.html..."

TEMPLATE="$PROJECT_ROOT/web/template.html"

if [ ! -f "$TEMPLATE" ]; then
    echo_error "Template not found: $TEMPLATE"
    exit 1
fi

# Escape special characters for sed replacement
escape_sed() {
    printf '%s' "$1" | sed 's/[&/\]/\\&/g'
}

# Build favicon tag
if [ -n "$FAVICON_FILE" ]; then
    FAVICON_TAG="<link rel=\"icon\" href=\"$FAVICON_FILE\">"
else
    FAVICON_TAG=""
fi

# Build meta description tag
if [ -n "$APP_DESCRIPTION" ]; then
    META_DESC_TAG="<meta name=\"description\" content=\"$(escape_sed "$APP_DESCRIPTION")\">"
    OG_DESC_TAG="<meta property=\"og:description\" content=\"$(escape_sed "$APP_DESCRIPTION")\">"
else
    META_DESC_TAG=""
    OG_DESC_TAG=""
fi

# Build og:title tag
OG_TITLE_TAG="<meta property=\"og:title\" content=\"$(escape_sed "$APP_NAME")\">"

# Build og:image tag if favicon exists
if [ -n "$FAVICON_FILE" ]; then
    OG_IMAGE_TAG="<meta property=\"og:image\" content=\"$FAVICON_FILE\">"
else
    OG_IMAGE_TAG=""
fi

# Perform template substitutions
sed \
    -e "s|{{APP_NAME}}|$(escape_sed "$APP_NAME")|g" \
    -e "s|{{FAVICON_TAG}}|$(escape_sed "$FAVICON_TAG")|g" \
    -e "s|{{META_DESC_TAG}}|$(escape_sed "$META_DESC_TAG")|g" \
    -e "s|{{OG_TITLE_TAG}}|$(escape_sed "$OG_TITLE_TAG")|g" \
    -e "s|{{OG_DESC_TAG}}|$(escape_sed "$OG_DESC_TAG")|g" \
    -e "s|{{OG_IMAGE_TAG}}|$(escape_sed "$OG_IMAGE_TAG")|g" \
    "$TEMPLATE" > "$OUTPUT_DIR/index.html"

# ── Done ─────────────────────────────────────────────────────────────────────

echo ""
echo_info "Web export complete!"
echo_info "Output: $OUTPUT_DIR"
echo ""
echo "  Files:"
for f in "$OUTPUT_DIR"/*; do
    SIZE=$(wc -c < "$f" | tr -d ' ')
    if [ "$SIZE" -ge 1048576 ]; then
        SIZE_STR="$(echo "scale=1; $SIZE / 1048576" | bc) MB"
    elif [ "$SIZE" -ge 1024 ]; then
        SIZE_STR="$(echo "scale=1; $SIZE / 1024" | bc) KB"
    else
        SIZE_STR="$SIZE B"
    fi
    echo "    $(basename "$f")  ($SIZE_STR)"
done
echo ""
echo "  To serve locally:"
echo "    cd $OUTPUT_DIR && python3 -m http.server 8080"
echo ""
