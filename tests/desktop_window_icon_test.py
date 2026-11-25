#!/usr/bin/env python3
"""Verify app.json window icons through the X11 window property."""

import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid
import zlib


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png():
    header = struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)
    row = b"\0" + bytes([255, 0, 0, 255]) * 2
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(row * 2)) + png_chunk(b"IEND", b""))


def run_case(budo, project, icon, expected=None, warning=None):
    title = "budo-icon-test-" + uuid.uuid4().hex
    metadata = {} if icon is None else {"icon": icon}
    (project / "app.json").write_text(json.dumps(metadata), encoding="utf-8")
    env = dict(os.environ, SDL_VIDEODRIVER="x11")
    process = subprocess.Popen([budo, "run", str(project), "--title", title],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               text=True, env=env)
    try:
        deadline = time.monotonic() + 20
        window_id = None
        while time.monotonic() < deadline and process.poll() is None:
            tree = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True)
            for line in tree.stdout.splitlines():
                if f'"{title}"' in line:
                    candidate = line.split()[0]
                    pid = subprocess.run(["xprop", "-id", candidate, "_NET_WM_PID"],
                                         capture_output=True, text=True).stdout
                    if re.search(rf"= {process.pid}\s*$", pid):
                        window_id = candidate
                        break
            if window_id:
                break
            time.sleep(0.1)
        if not window_id:
            raise AssertionError("application did not create its window")
        if expected:
            while time.monotonic() < deadline:
                prop = subprocess.run(["xprop", "-id", window_id, "-f",
                                       "_NET_WM_ICON", "32c", "_NET_WM_ICON"],
                                      capture_output=True, text=True, check=True).stdout
                numbers = re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", prop.split("=", 1)[-1])
                if len(numbers) >= 3:
                    actual = tuple(int(value, 0) for value in numbers[:3])
                    if actual == expected:
                        break
                time.sleep(0.1)
            else:
                raise AssertionError(f"unexpected window icon: {numbers[:3]}, expected {expected}")
    finally:
        process.terminate()
        try:
            _, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            _, stderr = process.communicate()
        if warning and warning not in stderr:
            raise AssertionError(f"missing icon warning {warning!r}: {stderr}")


def main():
    if (sys.platform != "linux" or not os.environ.get("DISPLAY")
            or not shutil.which("xprop") or not shutil.which("xwininfo")):
        print("skipped: X11, xprop and xwininfo are required")
        return 77
    if subprocess.run(["xwininfo", "-root"], capture_output=True).returncode:
        print("skipped: X11 display unavailable")
        return 77
    with tempfile.TemporaryDirectory(prefix="budo-icon-test-") as directory:
        project = Path(directory)
        (project / "main.js").write_text(
            "function frame() { sys.canvas.clear('#000'); sys.animation.requestFrame(frame); }\n"
            "sys.animation.requestFrame(frame);\n", encoding="utf-8")
        (project / "assets").mkdir()
        (project / "assets" / "icon.png").write_bytes(png())
        (project / "icon.svg").write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10">'
            '<rect width="10" height="10" fill="#0000ff" fill-opacity="0.5"/></svg>',
            encoding="utf-8")
        run_case(sys.argv[1], project, "assets/icon.png", (2, 2, 0xffff0000))
        run_case(sys.argv[1], project, "icon.svg", (128, 128, 0x800000ff))
        run_case(sys.argv[1], project, "missing.png", warning="could not load application icon")
        run_case(sys.argv[1], project, "../icon.png", warning="invalid application icon path")
        run_case(sys.argv[1], project, None)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
