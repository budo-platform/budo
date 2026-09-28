#!/usr/bin/env bash
# Generate website icon assets from the canonical budo-icon.svg.
#
# Outputs (into website/):
#   - budo-icon.svg      (copy of source, used inline by the site)
#   - favicon.png            (512x512, also referenced by OG/Twitter cards)
#   - favicon-32.png         (32x32)
#   - favicon-16.png         (16x16)
#   - apple-touch-icon.png   (180x180)
#
# Requires one of: rsvg-convert (preferred, brew install librsvg) or
# ImageMagick (`magick` or `convert`).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_SVG="$ROOT_DIR/budo-icon.svg"
OUT_DIR="$ROOT_DIR/website"

if [ ! -f "$SRC_SVG" ]; then
    echo "error: source SVG not found: $SRC_SVG" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# Pick a renderer.
RENDERER=""
if command -v rsvg-convert >/dev/null 2>&1; then
    RENDERER="rsvg"
elif command -v magick >/dev/null 2>&1; then
    RENDERER="magick"
elif command -v convert >/dev/null 2>&1; then
    RENDERER="convert"
else
    cat >&2 <<EOF
error: no SVG renderer found.

Install one of:
  - librsvg (preferred):   brew install librsvg
  - ImageMagick:           brew install imagemagick
EOF
    exit 1
fi

render_png() {
    local size="$1"
    local out="$2"
    case "$RENDERER" in
        rsvg)
            rsvg-convert -w "$size" -h "$size" "$SRC_SVG" -o "$out"
            ;;
        magick)
            magick -background none -density 384 "$SRC_SVG" \
                -resize "${size}x${size}" "$out"
            ;;
        convert)
            convert -background none -density 384 "$SRC_SVG" \
                -resize "${size}x${size}" "$out"
            ;;
    esac
    echo "  $(basename "$out")  (${size}x${size})"
}

echo "Generating website icons (renderer: $RENDERER)"

cp "$SRC_SVG" "$OUT_DIR/budo-icon.svg"
echo "  budo-icon.svg  (copied)"

render_png 512 "$OUT_DIR/favicon.png"
render_png 32  "$OUT_DIR/favicon-32.png"
render_png 16  "$OUT_DIR/favicon-16.png"
render_png 180 "$OUT_DIR/apple-touch-icon.png"

echo "Done. Icons written to $OUT_DIR/"
