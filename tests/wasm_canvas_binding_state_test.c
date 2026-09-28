#include "graphics/wasm_canvas_bindings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char CANVAS_STATE_WAT[] =
    "(module\n"
    "  (import \"env\" \"window_get_width\" (func $width (result i32)))\n"
    "  (import \"env\" \"input_get_mouse_x\" (func $mouse_x (result i32)))\n"
    "  (import \"env\" \"log_int\" (func $log_int (param i32)))\n"
    "  (func (export \"frame\") (param $expected f32)\n"
    "    i32.const 7 call $log_int\n"
    "    call $width\n"
    "    call $mouse_x\n"
    "    i32.add\n"
    "    local.get $expected\n"
    "    i32.trunc_f32_s\n"
    "    i32.ne\n"
    "    if unreachable end))\n";

static void write_wat_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(CANVAS_STATE_WAT, 1, strlen(CANVAS_STATE_WAT), file) ==
           strlen(CANVAS_STATE_WAT));
    assert(fclose(file) == 0);
}

static void load_wat(WasmCanvasContext *ctx, const char *path)
{
    if (!wasm_canvas_load_wat_file(ctx, path))
        fprintf(stderr, "Failed to load canvas state fixture: %s\n",
                wasm_canvas_get_error(ctx));
    assert(wasm_canvas_has_animation(ctx));
}

int main(int argc, char **argv)
{
    char wat_path[1024];
    InputState input_a;
    InputState input_b;
    WasmCanvasContext *runtime_a;
    WasmCanvasContext *runtime_b;

    assert(argc == 2);
    assert(snprintf(wat_path, sizeof(wat_path), "%s/canvas-state.wat", argv[1]) > 0);
    write_wat_file(wat_path);

    input_init(&input_a);
    input_init(&input_b);
    input_set_mouse_position(&input_a, 11, 31);
    input_set_mouse_position(&input_b, 22, 42);

    runtime_a = wasm_canvas_create(argv[1]);
    runtime_b = wasm_canvas_create(argv[1]);
    assert(runtime_a && runtime_b);

    wasm_canvas_set_context(runtime_a, NULL, &input_a, NULL, 100, 300, 1.0f);
    wasm_canvas_set_context(runtime_b, NULL, &input_b, NULL, 200, 400, 2.0f);
    load_wat(runtime_a, wat_path);
    load_wat(runtime_b, wat_path);

    assert(wasm_canvas_call_animation(runtime_a, 111.0));
    assert(wasm_canvas_call_animation(runtime_b, 222.0));
    assert(wasm_canvas_call_animation(runtime_a, 111.0));

    wasm_canvas_destroy(runtime_b);
    assert(wasm_canvas_call_animation(runtime_a, 111.0));
    wasm_canvas_destroy(runtime_a);

    assert(remove(wat_path) == 0);
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"canvas.resourceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"window.instanceState\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"scheduling.callback\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"runtime.peerCleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"console.call\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("WASM canvas binding state isolation test passed");
    return 0;
}