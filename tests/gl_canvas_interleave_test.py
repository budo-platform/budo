#!/usr/bin/env python3
"""Canvas drawing between raw GL draws lands in the canvas.

A frame that draws on the canvas, then with sys.gl (which rebinds the
framebuffer), then on the canvas again, then with sys.gl again, must keep both
canvas rectangles in the canvas: Skia re-reads the GL state before it flushes.
sys.canvas.readPixels() after GL drawing must read the canvas, upright.

Needs a window with a GL context; exits 77 (skipped) where none can be made.

Usage: gl_canvas_interleave_test.py <budo executable> <scratch directory> <shader directory>
"""

import shutil
import subprocess
import sys
from pathlib import Path

APP = r"""
const program = sys.gl.createProgram('image_preview.vert', 'image_preview.frag');
const pixel = new Uint8Array([0, 0, 255, 255]);
const texture = sys.gl.createTexture2D(1, 1, 'rgba8', pixel);
let frames = 0;
function frame() {
    sys.canvas.clear('#ffffff');
    sys.canvas.setFillColor('#ff0000');
    sys.canvas.drawRect(0, 0, 40, 40);              // before any GL
    sys.gl.bindScreen();
    sys.gl.useProgram(program);
    sys.gl.texture(program, 'u_image', texture, 1);
    sys.gl.drawRegionImmediate(program, 200, 0, 40, 40, -1);
    sys.canvas.setFillColor('#00ff00');
    sys.canvas.drawRect(0, 60, 40, 40);             // between two GL draws
    sys.gl.drawRegionImmediate(program, 200, 60, 40, 40, -1);
    if (++frames === 4) {
        const shot = sys.canvas.readPixels();
        const scale = shot.width / sys.window.getWidth();
        const at = (x, y) => Array.from(new Uint8Array(shot.pixels,
            (Math.round(y * scale) * shot.width + Math.round(x * scale)) * 4, 3)).join(',');
        console.log('PIXELS top=' + at(20, 20) + ' middle=' + at(20, 80) + ' gl=' + at(220, 20));
        sys.exit(0);
    }
    sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
"""


def main():
    budo, scratch, shaders = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    for name in ("image_preview.vert", "image_preview.frag"):
        shutil.copy(shaders / name, scratch / name)
    (scratch / "main.js").write_text(APP, encoding="utf-8")
    try:
        result = subprocess.run([budo, "run", str(scratch), "--width", "300", "--height", "200"],
                                capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        print("budo did not exit within 60 s", file=sys.stderr)
        return 1
    output = result.stdout + result.stderr
    line = next((l for l in output.splitlines() if l.startswith("PIXELS ")), None)
    if line is None:
        if "Exception" in output or "Error:" in output and "window" not in output.lower():
            print(output, file=sys.stderr)
            return 1
        print("skipped: no GL window here\n" + output)
        return 77
    values = dict(part.split("=") for part in line.split()[1:])
    errors = []
    if values["top"] != "255,0,0":
        errors.append(f"canvas drawn before GL: {values['top']}, expected red (readPixels must read the canvas, upright)")
    if values["middle"] != "0,255,0":
        errors.append(f"canvas drawn between GL draws: {values['middle']}, expected green (Skia flushed into the wrong framebuffer)")
    if values["gl"] != "255,255,255":
        errors.append(f"GL draws go to the screen, not the canvas: {values['gl']}, expected white")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("gl_canvas_interleave_test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
