#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cd "$root"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Skipping video fixture script test: ffmpeg is unavailable"
    exit 77
fi

if ! ffmpeg -hide_banner -encoders 2>/dev/null |
    awk '$2 == "libx264" { found = 1 } END { exit !found }'
then
    echo "Skipping video fixture script test: ffmpeg encoder libx264 is unavailable"
    exit 77
fi

rm -f assets/background.mp4 assets/inset.mp4
sh generate-fixtures.sh

test -s assets/background.mp4
test -s assets/inset.mp4

rm -f assets/background.mp4 assets/inset.mp4
