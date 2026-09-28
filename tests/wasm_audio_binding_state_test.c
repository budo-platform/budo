#include "audio/wasm_audio_bindings.h"
#include "tests/audio_mock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wasmtime.h>

typedef struct
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmAudioBindingState *audio_state;
    wasmtime_instance_t instance;
    wasmtime_func_t create_oscillator;
} AudioRuntime;

static const char AUDIO_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"audio_create_oscillator\" (func $create (result i32)))\n"
    "  (export \"create_oscillator\" (func $create)))";

static void fail_wasmtime_error(wasmtime_error_t *error)
{
    wasm_byte_vec_t message;
    wasmtime_error_message(error, &message);
    fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    assert(0);
}

static void fail_wasm_trap(wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    wasm_trap_message(trap, &message);
    fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
    assert(0);
}

static void audio_runtime_create(AudioRuntime *runtime, const char *asset_root)
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
    runtime->audio_state = wasm_audio_binding_state_create(asset_root);
    assert(runtime->audio_state);

    error = wasm_audio_register(runtime->linker, runtime->audio_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(AUDIO_MODULE_WAT, strlen(AUDIO_MODULE_WAT), &wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_module_new(runtime->engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime->module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_linker_instantiate(runtime->linker, runtime->context,
                                        runtime->module, &runtime->instance, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);

    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        "create_oscillator", 17, &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    runtime->create_oscillator = exported.of.func;
}

static void audio_runtime_destroy(AudioRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_audio_binding_state_destroy(runtime->audio_state);
    wasm_engine_delete(runtime->engine);
}

static int32_t create_oscillator(AudioRuntime *runtime)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(
        runtime->context, &runtime->create_oscillator,
        NULL, 0, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

int main(void)
{
    AudioRuntime runtime_a;
    AudioRuntime runtime_b;

    audio_mock_reset();
    audio_runtime_create(&runtime_a, "wasm-a");
    audio_runtime_create(&runtime_b, "wasm-b");
    assert(audio_mock_create_count() == 0);

    assert(create_oscillator(&runtime_a) ==
           audio_mock_asset_root_id("wasm-a"));
    assert(audio_mock_create_count() == 1);
    assert(create_oscillator(&runtime_b) ==
           audio_mock_asset_root_id("wasm-b"));
    assert(audio_mock_create_count() == 2);
    assert(create_oscillator(&runtime_a) ==
           audio_mock_asset_root_id("wasm-a"));

    audio_runtime_destroy(&runtime_b);
    assert(audio_mock_destroy_count() == 1);
    assert(audio_mock_live_count() == 1);
    assert(create_oscillator(&runtime_a) ==
           audio_mock_asset_root_id("wasm-a"));
    audio_runtime_destroy(&runtime_a);
    assert(audio_mock_destroy_count() == 2);
    assert(audio_mock_live_count() == 0);

    puts("WASM audio binding state isolation test passed");
    return 0;
}