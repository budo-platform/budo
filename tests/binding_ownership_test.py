#!/usr/bin/env python3
"""Enforce instance-owned state in every JS, Lua, and WASM binding file.

Binding state belongs to one runtime instance (see CODE-STRUCTURE.md, "Managed
binding ownership"). Mutable file-scope state would be shared by every runtime
in the process, and `#define g_name (state->field)` aliases make instance state
read like globals, so both are rejected outside an explicit allow-list.
"""

import importlib.util
import re
import sys
from pathlib import Path

# File-scope state that is process-wide by design, with the reason.
ALLOWED_STATE = {
    "src/file/js_file_bindings.c": {
        # Web and Android picker callbacks cannot retain C pointers across
        # runtime teardown; a bounded, generation-checked slot table bridges them.
        "g_file_bridge_slots",
        "g_file_bridge_mutex",
        "g_file_bridge_mutex_ready",
        "g_file_bridge_once",
    },
}

BINDING_GLOBS = (
    "src/*/js_*_bindings.c",
    "src/*/js_*_bindings.cpp",
    "src/*/lua_*_bindings.c",
    "src/*/lua_*_bindings.cpp",
    "src/*/wasm_*_bindings.c",
)

GLOBAL_ALIAS = re.compile(r"^\s*#\s*define\s+(g_\w+)\b", re.MULTILINE)
PROTOTYPE = re.compile(r"^static\s[^=(]*\b\w+\s*\([^*]")


def load_scanner(root):
    path = root / "tests" / "canvas_binding_ownership_test.py"
    spec = importlib.util.spec_from_file_location("canvas_binding_ownership", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def without_preprocessor_lines(source):
    """Blank out directives (with continuations) so a declaration that follows
    an #include or #define is still seen as a statement starting with static."""
    lines = source.split("\n")
    result = []
    continued = False
    for line in lines:
        directive = continued or line.lstrip().startswith("#")
        continued = directive and line.rstrip().endswith("\\")
        result.append("" if directive else line)
    return "\n".join(result)


def declared_name(declaration):
    before_value = declaration.rstrip(";").split("=", 1)[0]
    names = re.findall(r"\b([A-Za-z_]\w*)\b(?:\s*\[[^\]]*\])*\s*$", before_value.strip())
    return names[-1] if names else ""


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    scanner = load_scanner(root)
    errors = []
    files = sorted({path for pattern in BINDING_GLOBS for path in root.glob(pattern)})
    if not files:
        errors.append("no binding sources found")

    for path in files:
        relative = path.relative_to(root).as_posix()
        source = path.read_text(encoding="utf-8")
        allowed = ALLOWED_STATE.get(relative, set())

        for declaration in scanner.file_scope_statics(without_preprocessor_lines(source)):
            normalized = " ".join(declaration.split())
            if normalized.startswith("static const ") or PROTOTYPE.match(normalized):
                continue
            if declared_name(normalized) in allowed:
                continue
            errors.append(f"{relative}: mutable file-scope state: {normalized[:160]}")

        for alias in GLOBAL_ALIAS.findall(source):
            errors.append(f"{relative}: global-looking alias macro {alias}")

    if errors:
        for error in errors:
            print(f"binding_ownership_test: {error}", file=sys.stderr)
        return 1
    print(f"binding_ownership_test: ok ({len(files)} binding sources)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
