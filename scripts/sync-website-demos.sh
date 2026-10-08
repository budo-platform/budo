#!/usr/bin/env bash
# Sync examples into the website demos directory.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/_common.sh"

SOURCE_DIR="$PROJECT_ROOT/examples/"
DEST_DIR="$PROJECT_ROOT/website/demos/"

require_cmd rsync

mkdir -p "$DEST_DIR"

echo_step "Syncing examples to website/demos..."
# Local SQLite databases are app data, not demo content: never copy them.
# Excluded files are left alone on the website side, so a database committed
# under website/demos keeps its committed content. SQLite's -wal/-shm/-journal
# files exist only while an app has its database open; remove any stale copy.
# Build outputs (Android packages in dist/, build/, .budo/) and signing material
# are not demo content either, and must never be published.
rsync -a --delete \
    --exclude '.DS_Store' \
    --exclude 'dist/' \
    --exclude 'build/' \
    --exclude '.budo/' \
    --exclude 'keystore/' \
    --exclude '*.jks' \
    --exclude '*.keystore' \
    --exclude '__pycache__/' \
    --exclude '*.db' \
    --exclude '*.db-wal' \
    --exclude '*.db-shm' \
    --exclude '*.db-journal' \
    "$SOURCE_DIR" "$DEST_DIR"
find "$DEST_DIR" \( -name '*.db-wal' -o -name '*.db-shm' -o -name '*.db-journal' \) -delete

echo_info "Website demos synced: $DEST_DIR"