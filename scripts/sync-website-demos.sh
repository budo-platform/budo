#!/usr/bin/env bash
# Sync examples into the website demos directory.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

SOURCE_DIR="$PROJECT_ROOT/examples/"
DEST_DIR="$PROJECT_ROOT/website/demos/"

require_cmd rsync

mkdir -p "$DEST_DIR"

echo_step "Syncing examples to website/demos..."
rsync -a --delete \
    --exclude '.DS_Store' \
    --exclude '__pycache__/' \
    "$SOURCE_DIR" "$DEST_DIR"

echo_info "Website demos synced: $DEST_DIR"