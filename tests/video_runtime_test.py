from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    budo, ffmpeg = sys.argv[1:]
    encoders = subprocess.run(
        [ffmpeg, "-hide_banner", "-encoders"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    ).stdout
    if not any(len(line.split()) >= 2 and line.split()[1] == "libx264"
               for line in encoders.splitlines()):
        print("Skipping video runtime test: libx264 is unavailable")
        return 77

    with tempfile.TemporaryDirectory(prefix="budo-video-runtime-") as directory:
        root = Path(directory)
        subprocess.run(
            [
                ffmpeg, "-nostdin", "-v", "error", "-y", "-f", "lavfi",
                "-i", "testsrc2=size=64x48:rate=30:duration=1",
                "-vf", "setsar=1", "-c:v", "libx264", "-threads", "1",
                "-pix_fmt", "yuv420p",
                "-x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709",
                "-color_range", "tv", "-colorspace", "bt709",
                "-color_primaries", "bt709", "-color_trc", "bt709",
                str(root / "clip.mp4"),
            ],
            check=True,
        )
        main_script = root / "main.js"
        main_script.write_text("""
let reader = 0;
function fail(error) {
    console.log("VIDEO_RUNTIME_ERROR " + (error.message || error));
    sys.exit(1);
}
function request(ordinal) {
    if (!sys.video.requestFrame(reader, ordinal, {
        onFrame(frame, texture, ptsUs, durationUs, actualOrdinal) {
            if (actualOrdinal !== ordinal || texture <= 0 ||
                !sys.video.releaseFrame(frame)) return fail("invalid frame");
            if (ordinal === 0) return request(1);
            if (!sys.video.closeReader(reader)) return fail(sys.video.getError());
            console.log("VIDEO_RUNTIME_OK");
            sys.exit(0);
        },
        onEof() { fail("unexpected EOF"); },
        onError: fail,
    })) fail(sys.video.getError());
}
reader = sys.video.openReader("assets/clip.mp4", {
    onReady() { if (!reader) return fail("synchronous callback"); request(0); },
    onError: fail,
});
if (!reader) fail(sys.video.getError());
""", encoding="utf-8")
        result = subprocess.run(
            [budo, "run", str(root)], capture_output=True, text=True, timeout=30,
        )
        if "Failed to create window" in result.stderr:
            print("Skipping video runtime test: no graphics window available")
            return 77
        if result.returncode != 0 or "VIDEO_RUNTIME_OK" not in result.stdout:
            print(result.stdout)
            print(result.stderr, file=sys.stderr)
            return 1

        reload_script = main_script.read_text(encoding="utf-8").replace(
            "VIDEO_RUNTIME_OK", "VIDEO_RELOAD_OK")
        writable = root.parent / (root.name + "-files")
        writable.mkdir()
        try:
            main_script.write_text("""
let reader = sys.video.openReader("assets/clip.mp4", {
    onReady() {
        if (!sys.video.requestFrame(reader, 1, {
            onFrame() { console.log("STALE_VIDEO_CALLBACK"); sys.exit(2); },
            onEof() { sys.exit(3); },
            onError() { sys.exit(4); },
        })) sys.exit(5);
        sys.files.writeText("files/ready", "ready");
        while (!sys.files.exists("files/continue")) {}
    },
    onError() { sys.exit(6); },
});
if (!reader) sys.exit(7);
""", encoding="utf-8")
            process = subprocess.Popen(
                [budo, "run", str(root), "--watch", "--file-root", str(writable)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            try:
                deadline = time.monotonic() + 15
                while not (writable / "ready").exists() and process.poll() is None:
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Video watch test did not start")
                    time.sleep(0.01)
                if process.poll() is None:
                    main_script.write_text(reload_script, encoding="utf-8")
                    time.sleep(0.5)
                    (writable / "continue").write_text("continue", encoding="utf-8")
                stdout, stderr = process.communicate(timeout=30)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
            if "Failed to create window" in stderr:
                print("Skipping video runtime test: no graphics window available")
                return 77
            if (process.returncode != 0 or "VIDEO_RELOAD_OK" not in stdout or
                    "STALE_VIDEO_CALLBACK" in stdout):
                print(stdout)
                print(stderr, file=sys.stderr)
                return 1
        finally:
            for path in (writable / "ready", writable / "continue"):
                path.unlink(missing_ok=True)
            writable.rmdir()
    return 0


if __name__ == "__main__":
    sys.exit(main())
