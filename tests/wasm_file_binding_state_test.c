#include "file/wasm_file_bindings.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasmtime.h>

typedef struct WasmFileRuntime
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmFileBindingState *file_state;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
    wasmtime_func_t read_text;
} WasmFileRuntime;

static const char FILE_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"files_read_text\" (func $read_text (param i32 i32 i32 i32) (result i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (export \"read_text\" (func $read_text)))";

static void fail_wasmtime_error(wasmtime_error_t *error)
{
    wasm_byte_vec_t message;
    wasmtime_error_message(error, &message);
    fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    assert(false);
}

static void fail_wasm_trap(wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    wasm_trap_message(trap, &message);
    fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
    assert(false);
}

static void write_fixture(const char *root, const char *contents)
{
    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/identity.txt", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(contents, 1, strlen(contents), file) == strlen(contents));
    assert(fclose(file) == 0);
}

static void remove_fixture(const char *root)
{
    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/identity.txt", root);
    assert(remove(path) == 0);
}

static void wasm_file_runtime_create(WasmFileRuntime *runtime,
                                     const char *root)
{
    wasm_byte_vec_t wasm;
    wasmtime_extern_t exported;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;

    memset(runtime, 0, sizeof(*runtime));
    runtime->engine = wasm_engine_new();
    assert(runtime->engine);
    runtime->store = wasmtime_store_new(runtime->engine, NULL, NULL);
    assert(runtime->store);
    runtime->context = wasmtime_store_context(runtime->store);
    runtime->linker = wasmtime_linker_new(runtime->engine);
    assert(runtime->linker);
    runtime->file_state = wasm_file_binding_state_create(root);
    assert(runtime->file_state);

    error = wasm_file_register_state(runtime->linker, runtime->file_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(FILE_MODULE_WAT, strlen(FILE_MODULE_WAT), &wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_module_new(runtime->engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime->module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_linker_instantiate(runtime->linker, runtime->context,
                                        runtime->module, &runtime->instance,
                                        &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);

    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        "memory", 6, &exported));
    assert(exported.kind == WASMTIME_EXTERN_MEMORY);
    runtime->memory = exported.of.memory;
    wasm_file_binding_state_set_memory(runtime->file_state, runtime->context,
                                       &runtime->memory);

    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        "read_text", 9, &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    runtime->read_text = exported.of.func;
}

static void wasm_file_runtime_destroy(WasmFileRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_file_binding_state_destroy(runtime->file_state);
    wasm_engine_delete(runtime->engine);
    memset(runtime, 0, sizeof(*runtime));
}

static void assert_root_contents(WasmFileRuntime *runtime,
                                 const char *expected)
{
    static const char path[] = "assets/identity.txt";
    static const int32_t path_offset = 0;
    static const int32_t output_offset = 128;
    wasmtime_val_t args[4];
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    uint8_t *memory = wasmtime_memory_data(runtime->context, &runtime->memory);

    memcpy(memory + path_offset, path, sizeof(path) - 1);
    memset(memory + output_offset, 0, 128);
    for (size_t index = 0; index < 4; index++)
        args[index].kind = WASMTIME_I32;
    args[0].of.i32 = path_offset;
    args[1].of.i32 = (int32_t)(sizeof(path) - 1);
    args[2].of.i32 = output_offset;
    args[3].of.i32 = 128;

    wasmtime_error_t *error = wasmtime_func_call(
        runtime->context, &runtime->read_text, args, 4, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_I32);
    assert(result.of.i32 == (int32_t)strlen(expected));
    assert(memcmp(memory + output_offset, expected, strlen(expected)) == 0);
}

int main(int argc, char **argv)
{
    WasmFileRuntime runtime_a;
    WasmFileRuntime runtime_b;

    assert(argc == 3);
    write_fixture(argv[1], "wasm-root-a");
    write_fixture(argv[2], "wasm-root-b");

    wasm_file_runtime_create(&runtime_a, argv[1]);
    wasm_file_runtime_create(&runtime_b, argv[2]);
    assert_root_contents(&runtime_a, "wasm-root-a");
    assert_root_contents(&runtime_b, "wasm-root-b");

    wasm_file_runtime_destroy(&runtime_b);
    assert_root_contents(&runtime_a, "wasm-root-a");
    wasm_file_runtime_destroy(&runtime_a);

    remove_fixture(argv[1]);
    remove_fixture(argv[2]);
    puts("WASM file binding state isolation test passed");
    return 0;
}