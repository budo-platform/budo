#!/usr/bin/env python3

import pathlib
import sys


WINDOWS = (
    "src/core/window.c",
    "src/web/web_window.c",
)

# Render targets are implemented once in the include every backend uses.
SHARED_RESOURCES = "src/graphics/gl_window_resources.inc"

FORBIDDEN = (
    "static GLuint compile_shader(",
    "static GLuint link_program(",
)

REQUIRED = (
    "gl_shader_pipeline_create(",
)

SHARED_REQUIRED = (
    "gl_render_target_create(",
    "gl_render_target_destroy(",
    "gl_render_target_resize(",
)


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    errors = []

    for relative_path in WINDOWS:
        source = (root / relative_path).read_text(encoding="utf-8")
        for token in FORBIDDEN:
            if token in source:
                errors.append(
                    f"{relative_path}: duplicated graphics pipeline returned: {token}")
        for token in REQUIRED:
            if source.count(token) != 1:
                errors.append(
                    f"{relative_path}: expected one shared delegate call: {token}")
        for token in SHARED_REQUIRED:
            if token in source:
                errors.append(
                    f"{relative_path}: render targets belong in {SHARED_RESOURCES}: {token}")
        if '#include "graphics/gl_window_resources.inc"' not in source:
            errors.append(f"{relative_path}: missing {SHARED_RESOURCES}")

    shared = (root / SHARED_RESOURCES).read_text(encoding="utf-8")
    for token in SHARED_REQUIRED:
        if shared.count(token) != 1:
            errors.append(f"{SHARED_RESOURCES}: expected one shared delegate call: {token}")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("window_graphics_consolidation_test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
