#!/usr/bin/env bash
# Fetch stb_image.h from the upstream nothings/stb repository.
#
# Budo's image loader (src/graphics/image_loader.c) needs this single
# header in third_party/stb/. Run this script once after a fresh clone:
#
#   ./scripts/setup-stb.sh
#
# Idempotent: re-running with the file already present is a no-op unless
# FORCE=1 is set in the environment.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

# ── Configuration ────────────────────────────────────────────────────────────

# Update the shared dependency lock when bumping the stb identity.
STB_REPO_URL="${STB_REPO_URL:-$(budo_dependency_lock_value BUDO_STB_GIT_REPOSITORY)}"
STB_COMMIT="${STB_COMMIT:-$(budo_dependency_lock_value BUDO_STB_GIT_TAG)}"

# SHA-256 of stb_image.h at the pinned commit. Leave empty to skip checking
# (not recommended). When updating STB_COMMIT, refresh this with:
#   sha256sum third_party/stb/stb_image.h
STB_IMAGE_SHA256="${STB_IMAGE_SHA256:-$(budo_dependency_lock_value BUDO_STB_IMAGE_SHA256)}"

STB_DIR="$PROJECT_ROOT/third_party/stb"
STB_IMAGE_HEADER="$STB_DIR/stb_image.h"

echo_step "stb Setup Script"

# ── Skip if already present ──────────────────────────────────────────────────

if [ -f "$STB_IMAGE_HEADER" ] && [ "${FORCE:-0}" != "1" ]; then
    echo_info "stb_image.h already present at $STB_IMAGE_HEADER"
    echo_info "Set FORCE=1 to re-download."
    exit 0
fi

# ── Requirements ─────────────────────────────────────────────────────────────

require_cmd git

# ── Fetch into a temporary shallow clone ─────────────────────────────────────

mkdir -p "$STB_DIR"

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

echo_info "Cloning $STB_REPO_URL (commit ${STB_COMMIT:0:10})..."
git -C "$TMP_DIR" init -q
# Check files out byte-for-byte (Git for Windows defaults to CRLF conversion),
# so the checksum below matches on every platform.
git -C "$TMP_DIR" config core.autocrlf false
git -C "$TMP_DIR" config core.eol lf
git -C "$TMP_DIR" remote add origin "$STB_REPO_URL"
git -C "$TMP_DIR" fetch --depth 1 -q origin "$STB_COMMIT"
git -C "$TMP_DIR" checkout -q FETCH_HEAD

if [ ! -f "$TMP_DIR/stb_image.h" ]; then
    echo_error "stb_image.h not found in upstream checkout at commit $STB_COMMIT"
    exit 1
fi

# ── Verify checksum (if pinned) ──────────────────────────────────────────────

if [ -n "$STB_IMAGE_SHA256" ]; then
    if command -v sha256sum &>/dev/null; then
        actual_sha="$(sha256sum "$TMP_DIR/stb_image.h" | awk '{print $1}')"
    elif command -v shasum &>/dev/null; then
        actual_sha="$(shasum -a 256 "$TMP_DIR/stb_image.h" | awk '{print $1}')"
    else
        echo_error "Neither sha256sum nor shasum available for checksum verification"
        exit 1
    fi

    if [ "$actual_sha" != "$STB_IMAGE_SHA256" ]; then
        echo_error "Checksum mismatch for stb_image.h"
        echo_error "  expected: $STB_IMAGE_SHA256"
        echo_error "  actual:   $actual_sha"
        exit 1
    fi
    echo_info "Checksum OK ($actual_sha)"
else
    echo_warn "STB_IMAGE_SHA256 is empty — skipping checksum verification."
fi

# ── Install ──────────────────────────────────────────────────────────────────

install -m 0644 "$TMP_DIR/stb_image.h" "$STB_IMAGE_HEADER"

echo_info "Installed: $STB_IMAGE_HEADER"
echo_step  "Done."
