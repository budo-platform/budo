#!/usr/bin/env python3
"""Enforce instance-owned canvas binding state across managed runtimes."""

import re
import sys
from pathlib import Path


FILES = {
    "javascript": Path("src/graphics/js_canvas_bindings.c"),
    "lua": Path("src/graphics/lua_canvas_bindings.c"),
    "wasmtime": Path("src/graphics/wasm_canvas_bindings.c"),
}


def strip_comments_and_strings(source):
    result = []
    index = 0
    state = "code"
    quote = ""
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and next_char == "*":
                result.extend("  ")
                index += 2
                state = "block_comment"
                continue
            if char == "/" and next_char == "/":
                result.extend("  ")
                index += 2
                state = "line_comment"
                continue
            if char in ('"', "'"):
                quote = char
                result.append(" ")
                index += 1
                state = "string"
                continue
            result.append(char)
            index += 1
            continue
        if state == "block_comment":
            if char == "*" and next_char == "/":
                result.extend("  ")
                index += 2
                state = "code"
            else:
                result.append("\n" if char == "\n" else " ")
                index += 1
            continue
        if state == "line_comment":
            result.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
            continue
        if char == "\\":
            result.extend("  ")
            index += 2
        elif char == quote:
            result.append(" ")
            index += 1
            state = "code"
        else:
            result.append("\n" if char == "\n" else " ")
            index += 1
    return "".join(result)


def file_scope_statics(source):
    clean = strip_comments_and_strings(source)
    depth = 0
    statement = []
    declarations = []
    for char in clean:
        if depth == 0:
            statement.append(char)
        if char == "{":
            if depth == 0:
                text = "".join(statement).strip()
                if text.startswith("static ") and "(" not in text:
                    declarations.append(text)
                statement = []
            depth += 1
        elif char == "}":
            depth = max(0, depth - 1)
            if depth == 0:
                statement = []
        elif char == ";" and depth == 0:
            text = "".join(statement).strip()
            if text.startswith("static "):
                declarations.append(text)
            statement = []
    return declarations


def require(source, pattern, label, errors):
    if not re.search(pattern, source, re.MULTILINE | re.DOTALL):
        errors.append(f"missing {label}")


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    errors = []
    sources = {}
    for runtime, relative in FILES.items():
        path = root / relative
        source = path.read_text(encoding="utf-8")
        sources[runtime] = source
        for declaration in file_scope_statics(source):
            normalized = " ".join(declaration.split())
            if normalized.startswith("static const "):
                continue
            errors.append(f"{relative}: mutable file-scope state: {normalized[:160]}")

    js = sources["javascript"]
    require(js, r"JS_SetContextOpaque\(ctx->context,\s*ctx\)",
            "QuickJS context opaque ownership", errors)
    require(js, r"JS_SetRuntimeOpaque\(ctx->runtime,\s*ctx\)",
            "QuickJS runtime opaque ownership", errors)
    require(js, r"JS_SetContextOpaque\(ctx->context,\s*NULL\)",
            "QuickJS context opaque teardown", errors)
    require(js, r"JS_SetRuntimeOpaque\(ctx->runtime,\s*NULL\)",
            "QuickJS runtime opaque teardown", errors)
    require(js, r"JS_GetContextOpaque\(ctx\)",
            "QuickJS callback context lookup", errors)

    lua = sources["lua"]
    require(lua, r"static const char lua_canvas_context_registry_key",
            "immutable Lua registry identity token", errors)
    require(lua, r"lua_rawsetp\(L,\s*LUA_REGISTRYINDEX,\s*&lua_canvas_context_registry_key\)",
            "Lua instance registry storage", errors)
    require(lua, r"lua_rawgetp\(L,\s*LUA_REGISTRYINDEX,\s*&lua_canvas_context_registry_key\)",
            "Lua callback context lookup", errors)
    require(lua, r"lua_canvas_store_context\(L,\s*NULL\)",
            "Lua registry teardown", errors)

    wasm = sources["wasmtime"]
    require(wasm, r"wasmtime_linker_define_func\([^;]+callback,\s*ctx,\s*NULL\)",
            "Wasmtime callback environment ownership", errors)
    require(wasm, r"#define CANVAS_CALLBACK_CONTEXT\s+\(\(WasmCanvasContext \*\)env\)",
            "Wasmtime callback environment lookup", errors)

    if errors:
        for error in errors:
            print(f"canvas_binding_ownership_test: {error}", file=sys.stderr)
        return 1
    print("canvas binding ownership checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
