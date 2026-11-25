# Picture-in-picture video demo

This experimental desktop QuickJS example uses `sys.video`, available by
default in fresh macOS/Linux desktop builds (`ENABLE_VIDEO=ON`). Preview and
export are silent:
audio, subtitles, and all other tracks from both inputs are omitted.

## Build and run

From the repository root, install FFmpeg 5 or newer development libraries,
`pkg-config`, and an FFmpeg build with the `libx264` encoder. Then run:

```sh
make CMAKE_FLAGS="-DENABLE_VIDEO=ON" build
sh examples/video_pip/generate-fixtures.sh
./build/budo run examples/video_pip
```

Run the fixture-generation command before starting the app; it writes the
generated videos under `assets/`, and those files are intentionally ignored by
Git. If they are missing, the example shows this command in its status text
and does not start a failed preview.

The fixture script writes `assets/background.mp4` (3 seconds) and
`assets/inset.mp4` (2 seconds). Both are generated, square-pixel, progressive
BT.709 SDR, 30 fps H.264 clips. To use other inputs, replace those files with
inputs in the documented supported subset.

The first input fills the 640x360 frame; the second is shown at 160x90 in the
lower-right corner with a 16-pixel margin. Use the on-screen controls to
play/pause, restart at the beginning, close or reopen the preview, and export
to `files/picture-in-picture.mp4` or cancel an active export. Restart and
seeking preserve the current play/pause state. At the end, Play restarts
playback; the retained final frame can also be sought.

The position slider becomes available once the actual shorter-input duration
is ready. Drag or click it and release to seek; Tab focuses controls, Space or
Enter activates buttons, and the slider supports Left/Right and Home/End.
Seeks are asynchronous, so the video briefly clears until the requested frame
arrives. Each seek reopens both inputs, jumps to an earlier container
random-access point and decodes forward to the selected 30 fps frame. Latency
depends on the distance to that point, so longer GOPs can take longer; seeks
are not constant-time. Export remains independent of preview.
Preview and export statuses are shown separately; the completed export path
is reported only after successful finalization. If reopening immediately
after close reports that the preview is busy, retry Open after cleanup.

The UI uses a synchronized copy of the canonical
[`budo-ui`](../budo-ui/ui/README.md) library. Refresh example copies with
`python3 scripts/sync-budo-ui.py`; check them with `--check`.
Run the focused UI and player tests with:

```sh
node --test examples/video_pip/player.test.mjs examples/video_pip/ui.test.mjs
```

The demo requires the `ffmpeg` executable and `libx264` for fixture generation;
it requires `libx264` in the FFmpeg development libraries used by the build for
export. It is not a distribution or licensing recommendation.
