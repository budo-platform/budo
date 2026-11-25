from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    test, preview_test, player_test, binding_test, ffmpeg = sys.argv[1:]
    encoders = subprocess.run(
        [ffmpeg, "-hide_banner", "-encoders"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    ).stdout
    if not any(len(line.split()) >= 2 and line.split()[1] == "libx264"
               for line in encoders.splitlines()):
        print("Skipping video composition test: ffmpeg encoder libx264 is unavailable")
        return 77

    with tempfile.TemporaryDirectory(prefix="budo-video-composition-") as directory:
        root = Path(directory)
        for name, color, duration in (
            ("background", "red", 3),
            ("inset", "blue", 2),
            ("not-30-fps", "green", 2),
        ):
            subprocess.run(
                [
                    ffmpeg, "-nostdin", "-v", "error", "-y",
                    "-f", "lavfi", "-i",
                    f"color=c={color}:size=64x48:"
                    f"rate={24 if name == 'not-30-fps' else 30}:duration={duration}",
                    "-vf", "setsar=1", "-c:v", "libx264", "-threads", "1",
                    "-pix_fmt", "yuv420p",
                    "-x264-params",
                    "colorprim=bt709:transfer=bt709:colormatrix=bt709",
                    "-color_range", "tv", "-colorspace", "bt709",
                    "-color_primaries", "bt709", "-color_trc", "bt709",
                    str(root / f"{name}.mp4"),
                ],
                check=True,
            )
        subprocess.run(
            [
                ffmpeg, "-nostdin", "-v", "error", "-y",
                "-f", "lavfi", "-i",
                "testsrc2=size=64x48:rate=30:duration=2",
                "-vf", "setsar=1", "-c:v", "libx264", "-threads", "1",
                "-pix_fmt", "yuv420p", "-g", "60", "-bf", "3",
                "-sc_threshold", "0",
                "-x264-params",
                "colorprim=bt709:transfer=bt709:colormatrix=bt709:"
                "keyint=60:min-keyint=60:scenecut=0:bframes=3",
                "-color_range", "tv", "-colorspace", "bt709",
                "-color_primaries", "bt709", "-color_trc", "bt709",
                str(root / "ordinal-bframes.mp4"),
            ],
            check=True,
        )
        subprocess.run([test, str(root)], check=True)
        subprocess.run([preview_test, str(root)], check=True)
        subprocess.run([player_test, str(root)], check=True)
        subprocess.run([binding_test, str(root)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
