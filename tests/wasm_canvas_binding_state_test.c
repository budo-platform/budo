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

static const char IMAGE_STATE_WAT[] =
    "(module\n"
    " (import \"env\" \"gl_image_load_async\" (func $load (param i32 i32 i32 i32 i32 i32 i32) (result i32)))\n"
    " (import \"env\" \"gl_image_status\" (func $status (param i32 i32) (result i32)))\n"
    " (import \"env\" \"gl_image_take_texture\" (func $take (param i32) (result i32)))\n"
    " (import \"env\" \"gl_image_release\" (func $release (param i32) (result i32)))\n"
    " (memory (export \"memory\") 1 2)\n"
    " (data (i32.const 1024) \"missing.tga\")\n"
    " (func (export \"frame\") (param f32) (local $index i32) (local $id i32)\n"
    "  i32.const 65530 i32.const 20 i32.const 2 i32.const 0 i32.const 0 i32.const 0 i32.const 0 call $load if unreachable end\n"
    "  block $done loop $submit\n"
    "   local.get $index i32.const 8 i32.ge_u br_if $done\n"
    "   i32.const 1024 i32.const 11 i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 call $load local.tee $id i32.eqz if unreachable end\n"
    "   local.get $index i32.const 4 i32.mul local.get $id i32.store\n"
    "   local.get $index i32.const 1 i32.add local.set $index br $submit\n"
    "  end end\n"
    "  i32.const 1024 i32.const 11 i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 call $load if unreachable end\n"
    "  i32.const 0 i32.load i32.const 65500 call $status i32.const -1 i32.ne if unreachable end\n"
    "  i32.const 1 memory.grow i32.const 1 i32.ne if unreachable end\n"
    "  i32.const 0 i32.load i32.const 65536 call $status if unreachable end\n"
    "  i32.const 65536 i32.load if unreachable end\n"
    "  i32.const 0 i32.load call $take if unreachable end\n"
    "  i32.const 0 local.set $index\n"
    "  block $finished loop $release_loop\n"
    "   local.get $index i32.const 8 i32.ge_u br_if $finished\n"
    "   local.get $index i32.const 4 i32.mul i32.load local.tee $id call $release i32.const 1 i32.ne if unreachable end\n"
    "   local.get $id call $release if unreachable end\n"
    "   local.get $index i32.const 1 i32.add local.set $index br $release_loop\n"
    "  end end))\n";

static void write_wat_file(const char *path, const char *source)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(source, 1, strlen(source), file) == strlen(source));
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
    write_wat_file(wat_path, CANVAS_STATE_WAT);

    input_init(&input_a);
    input_init(&input_b);
    input_set_mouse_position(&input_a, 11, 31);
    input_set_mouse_position(&input_b, 22, 42);

    runtime_a = wasm_canvas_create(argv[1], NULL);
    runtime_b = wasm_canvas_create(argv[1], NULL);
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

    write_wat_file(wat_path, IMAGE_STATE_WAT);
    runtime_a = wasm_canvas_create(argv[1], NULL);
    runtime_b = wasm_canvas_create(argv[1], NULL);
    assert(runtime_a && runtime_b);
    load_wat(runtime_a, wat_path);
    load_wat(runtime_b, wat_path);
    assert(wasm_canvas_call_animation(runtime_a, 0));
    assert(wasm_canvas_call_animation(runtime_b, 0));
    ImageAsyncOptions options = {0};
    uint32_t request_a = wasm_canvas_image_load(runtime_a, "assets/missing.tga", NULL, 0, &options);
    uint32_t request_b = wasm_canvas_image_load(runtime_b, "assets/missing.tga", NULL, 0, &options);
    assert(request_a && request_b);
    uint8_t result[312];
    assert(wasm_canvas_image_release(runtime_a, request_a));
    assert(wasm_canvas_image_status(runtime_a, request_a, result) == -1);
    assert(wasm_canvas_image_take(runtime_a, request_a) == 0);
    assert(!wasm_canvas_image_release(runtime_a, request_a));
    uint32_t replacement = wasm_canvas_image_load(runtime_a, "assets/missing.tga", NULL, 0, &options);
    assert(replacement && replacement != request_a);
    assert(wasm_canvas_image_status(runtime_a, request_a, result) == -1);
    wasm_canvas_destroy(runtime_a);
    runtime_a = wasm_canvas_create(argv[1], NULL);
    assert(runtime_a);
    load_wat(runtime_a, wat_path);
    assert(wasm_canvas_image_load(runtime_a, "assets/missing.tga", NULL, 0, &options));
    assert(wasm_canvas_image_status(runtime_b, request_b, result) == 0);
    assert(wasm_canvas_image_release(runtime_b, request_b));
    wasm_canvas_destroy(runtime_a);
    wasm_canvas_destroy(runtime_b);

    assert(remove(wat_path) == 0);
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"canvas.resourceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"window.instanceState\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"scheduling.callback\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"runtime.peerCleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"console.call\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("WASM canvas binding state isolation test passed");
    return 0;
}