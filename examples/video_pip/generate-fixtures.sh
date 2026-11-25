#!/bin/sh
set -eu

cd "$(dirname "$0")"
mkdir -p assets

ffmpeg -hide_banner -nostdin -v error -y \
    -f lavfi -i 'testsrc2=size=640x360:rate=30:duration=3' \
    -vf setsar=1 -c:v libx264 -threads 1 -pix_fmt yuv420p \
    -x264-params 'colorprim=bt709:transfer=bt709:colormatrix=bt709' \
    -color_range tv -colorspace bt709 -color_primaries bt709 \
    -color_trc bt709 assets/background.mp4

ffmpeg -hide_banner -nostdin -v error -y \
    -f lavfi -i 'smptebars=size=640x360:rate=30:duration=2' \
    -vf setsar=1 -c:v libx264 -threads 1 -pix_fmt yuv420p \
    -x264-params 'colorprim=bt709:transfer=bt709:colormatrix=bt709' \
    -color_range tv -colorspace bt709 -color_primaries bt709 \
    -color_trc bt709 assets/inset.mp4
