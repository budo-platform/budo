#!/usr/bin/env python3
"""Enforce the shared Skia/GL transition and GPU resource boundary."""

import re
import sys
from pathlib import Path


BACKENDS = (
    Path("src/core/window.c"),
    Path("src/web/web_window.c"),
    Path("private/android/android/app/src/main/cpp/android_window.c"),
)
ALLOWED_FLUSH_FUNCTIONS = {
    "flush_canvas_texture_transition",
    "flush_skia_canvas_transition",
    "flush_skia_canvas_discarding_gl_state",
}


def strip_comments(source):
    return re.sub(r"/\*.*?\*/|//[^\n]*", lambda match: "\n" * match.group(0).count("\n"),
                  source, flags=re.DOTALL)


def mask_function(source, name):
    match = re.search(rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", source)
    if not match:
        return source
    depth = 1
    index = match.end()
    while index < len(source) and depth:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    if depth:
        return source
    return source[:match.start()] + " " * (index - match.start()) + source[index:]


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    errors = []
    for relative in BACKENDS:
        source = (root / relative).read_text(encoding="utf-8")
        scan_source = strip_comments(source)
        if '#include "graphics/gl_state_guard_gl.inc"' not in source:
            errors.append(f"{relative}: missing shared GL transition guard")
        if '#include "graphics/gpu_resource_state.h"' not in source:
            errors.append(f"{relative}: missing shared GPU resource state")
        for alias in (
            "typedef GpuRenderTargetSlot GLRenderTarget;",
            "typedef GpuBufferSlot GLBufferSlot;",
            "typedef GpuTextureSlot GLTextureSlot;",
        ):
            if alias not in source:
                errors.append(f"{relative}: missing shared slot alias: {alias}")
        if re.search(r"typedef\s+struct\s*\{[^}]*bool\s+in_use;[^}]*\}\s*GL(?:RenderTarget|BufferSlot|TextureSlot)",
                     source, re.DOTALL):
            errors.append(f"{relative}: backend-local shared GPU slot definition remains")
        if "gpu_resource_slot_lookup(" not in source:
            errors.append(f"{relative}: shared resource ID validation is unused")
        if "flush_skia_canvas_preserving_gl_state(" not in source:
            errors.append(f"{relative}: no preserving Skia/GL transition")
        if "flush_skia_canvas_discarding_gl_state(" not in source:
            errors.append(f"{relative}: no explicit terminal Skia/GL transition")

        for function in ALLOWED_FLUSH_FUNCTIONS:
            scan_source = mask_function(scan_source, function)
        for match in re.finditer(r"\bskia_canvas_flush\s*\(", scan_source):
            line = source.count("\n", 0, match.start()) + 1
            errors.append(
                f"{relative}:{line}: direct Skia flush outside transition policy"
            )

    if errors:
        for error in errors:
            print(f"gpu_boundary_invariants_test: {error}", file=sys.stderr)
        return 1
    print("GPU boundary invariant checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
