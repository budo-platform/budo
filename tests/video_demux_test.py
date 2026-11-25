import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    test, ffmpeg = sys.argv[1:]
    encoders = subprocess.run(
        [ffmpeg, "-hide_banner", "-encoders"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    ).stdout
    for encoder in ("libx264", "aac", "mov_text"):
        if not any(len(line.split()) >= 2 and line.split()[1] == encoder
                   for line in encoders.splitlines()):
            print(f"Skipping video demux test: ffmpeg encoder {encoder} is unavailable")
            return 77
    with tempfile.TemporaryDirectory(prefix="budo-video-test-") as directory:
        root = Path(directory)
        english_subtitles = root / "english.srt"
        english_subtitles.write_text(
            "1\n00:00:00,000 --> 00:00:00,800\nHello\n", encoding="utf-8"
        )
        french_subtitles = root / "french.srt"
        french_subtitles.write_text(
            "1\n00:00:00,000 --> 00:00:00,800\nBonjour\n", encoding="utf-8"
        )
        clip = root / "clip.mp4"
        unsupported_color_clip = root / "unsupported-color.mp4"
        long_gop_clip = root / "long-gop.mp4"
        subprocess.run(
            [
                ffmpeg, "-nostdin", "-v", "error", "-y",
                "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=8:duration=1",
                "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1",
                "-i", str(english_subtitles), "-i", str(french_subtitles),
                "-map", "0:v", "-map", "1:a", "-map", "1:a",
                "-map", "2:s", "-map", "3:s",
                "-c:v", "libx264", "-threads", "1", "-pix_fmt", "yuv420p",
                "-x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709",
                "-aspect", "4:3", "-color_range", "tv", "-colorspace", "bt709",
                "-color_primaries", "bt709", "-color_trc", "bt709",
                "-c:a", "aac", "-c:s", "mov_text", "-metadata:s:s:0", "language=eng",
                "-metadata:s:s:0", "title=English captions",
                "-disposition:s:0", "default+forced",
                "-metadata:s:s:1", "language=fra",
                "-metadata:s:s:1", "title=French captions",
                "-disposition:s:1", "0",
                str(clip),
            ],
            check=True,
        )
        subprocess.run(
            [
                ffmpeg, "-nostdin", "-v", "error", "-y",
                "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=30:duration=6",
                "-c:v", "libx264", "-threads", "1", "-pix_fmt", "yuv420p",
                "-g", "60", "-keyint_min", "60", "-sc_threshold", "0",
                "-bf", "3",
                "-x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709",
                "-color_range", "tv", "-colorspace", "bt709",
                "-color_primaries", "bt709", "-color_trc", "bt709",
                str(long_gop_clip),
            ],
            check=True,
        )
        subprocess.run(
            [
                ffmpeg, "-nostdin", "-v", "error", "-y",
                "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=8:duration=1",
                "-c:v", "libx264", "-threads", "1", "-pix_fmt", "yuv420p",
                "-x264-params",
                "colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc",
                "-color_range", "tv", "-colorspace", "bt2020nc",
                "-color_primaries", "bt2020", "-color_trc", "smpte2084",
                str(unsupported_color_clip),
            ],
            check=True,
        )
        if os.name != "nt":
            os.symlink(clip, root / "link.mp4")
        subprocess.run([test, str(root)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
