#!/usr/bin/env python3

import pathlib
import sys


ADAPTERS = {
    # JavaScript transforms are part of sys.canvas (see js_canvas_bindings.c);
    # only the GL surface is split out.
    "javascript": ("src/graphics/js_canvas_bindings.c", 3200,
           ("js_gl_register",)),
    "lua": ("src/graphics/lua_canvas_bindings.c", 2000,
        ("lua_transform_register", "lua_gl_register")),
    "wasmtime": ("src/graphics/wasm_canvas_bindings.c", 2500,
         ("wasm_transform_register", "wasm_gl_register")),
}

FORBIDDEN = (
    "js_canvas_translate(",
    "l_canvas_translate(",
    "host_canvas_translate(",
    "static JSValue js_gl_",
    "static int l_gl_",
    "static wasm_trap_t *host_gl_",
)


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    errors = []

    for runtime, (relative_path, old_limit, registrations) in ADAPTERS.items():
        source = (root / relative_path).read_text(encoding="utf-8")
        line_count = len(source.splitlines())
        if line_count >= old_limit:
            errors.append(
                f"{runtime}: {relative_path} regressed to {line_count} lines")
        for registration in registrations:
            if source.count(registration + "(") != 1:
                errors.append(
                    f"{runtime}: expected one {registration} registration call")
        for token in FORBIDDEN:
            if token in source:
                errors.append(
                    f"{runtime}: transform implementation returned to {relative_path}: {token}")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("canvas_adapter_split_test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
