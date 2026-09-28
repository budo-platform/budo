#include "magneto/wasm_magneto_bindings.h"

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
    WasmMagnetoBindingState *magneto_state;
    wasmtime_instance_t instance;
    wasmtime_func_t start;
    wasmtime_func_t stop;
    wasmtime_func_t is_active;
    wasmtime_func_t get_accel_x;
} MagnetoRuntime;

static const char MAGNETO_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"magneto_start\" (func $start (result i32)))\n"
    "  (import \"env\" \"magneto_stop\" (func $stop))\n"
    "  (import \"env\" \"magneto_is_active\" (func $is_active (result i32)))\n"
    "  (import \"env\" \"magneto_get_accel_x\" (func $get_accel_x (result f32)))\n"
    "  (export \"start\" (func $start))\n"
    "  (export \"stop\" (func $stop))\n"
    "  (export \"is_active\" (func $is_active))\n"
    "  (export \"get_accel_x\" (func $get_accel_x)))";

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

static wasmtime_func_t get_function(MagnetoRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        name, strlen(name), &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void magneto_runtime_create(MagnetoRuntime *runtime)
{
    wasm_byte_vec_t wasm;
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
    runtime->magneto_state = wasm_magneto_binding_state_create();
    assert(runtime->magneto_state);

    error = wasm_magneto_register(runtime->linker, runtime->magneto_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(MAGNETO_MODULE_WAT, strlen(MAGNETO_MODULE_WAT), &wasm);
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

    runtime->start = get_function(runtime, "start");
    runtime->stop = get_function(runtime, "stop");
    runtime->is_active = get_function(runtime, "is_active");
    runtime->get_accel_x = get_function(runtime, "get_accel_x");
}

static void magneto_runtime_destroy(MagnetoRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_magneto_binding_state_destroy(runtime->magneto_state);
    wasm_engine_delete(runtime->engine);
}

static int32_t call_i32(MagnetoRuntime *runtime, wasmtime_func_t *function)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 NULL, 0, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static float call_f32(MagnetoRuntime *runtime, wasmtime_func_t *function)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 NULL, 0, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_F32);
    return result.of.f32;
}

static void call_void(MagnetoRuntime *runtime, wasmtime_func_t *function)
{
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 NULL, 0, NULL, 0, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
}

int main(void)
{
    MagnetoRuntime runtime_a;
    MagnetoRuntime runtime_b;
    float value_a;
    float value_b;

    magneto_runtime_create(&runtime_a);
    assert(call_i32(&runtime_a, &runtime_a.start) == 1);
    value_a = call_f32(&runtime_a, &runtime_a.get_accel_x);
    assert(value_a != 0.0f);

    magneto_runtime_create(&runtime_b);
    assert(call_i32(&runtime_b, &runtime_b.start) == 1);
    value_b = call_f32(&runtime_b, &runtime_b.get_accel_x);
    assert(value_b != 0.0f && value_b != value_a);
    assert(call_f32(&runtime_a, &runtime_a.get_accel_x) == value_a);

    call_void(&runtime_b, &runtime_b.stop);
    assert(call_i32(&runtime_b, &runtime_b.is_active) == 0);
    assert(call_i32(&runtime_a, &runtime_a.is_active) == 1);
    magneto_runtime_destroy(&runtime_b);
    assert(call_f32(&runtime_a, &runtime_a.get_accel_x) == value_a);
    magneto_runtime_destroy(&runtime_a);

    puts("WASM magneto binding state isolation test passed");
    return 0;
}