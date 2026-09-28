#include "math/wasm_math_bindings.h"

#include <assert.h>
#include <math.h>
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
    WasmMathBindingState *math_state;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
    wasmtime_func_t run;
} MathRuntime;

static const char MATH_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"math_vec3_scale\"\n"
    "    (func $math_vec3_scale (param i32 i32 f32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (func (export \"run\") (param i32 i32 f32)\n"
    "    local.get 0\n"
    "    local.get 1\n"
    "    local.get 2\n"
    "    call $math_vec3_scale))";

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

static void math_runtime_create(MathRuntime *runtime)
{
    wasm_byte_vec_t wasm;
    wasmtime_extern_t exported;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;
    bool found;

    memset(runtime, 0, sizeof(*runtime));
    runtime->engine = wasm_engine_new();
    assert(runtime->engine);
    runtime->store = wasmtime_store_new(runtime->engine, NULL, NULL);
    assert(runtime->store);
    runtime->context = wasmtime_store_context(runtime->store);
    runtime->linker = wasmtime_linker_new(runtime->engine);
    assert(runtime->linker);
    runtime->math_state = wasm_math_binding_state_create();
    assert(runtime->math_state);

    error = wasm_math_register(runtime->linker, runtime->math_state);
    if (error)
        fail_wasmtime_error(error);

    error = wasmtime_wat2wasm(MATH_MODULE_WAT, strlen(MATH_MODULE_WAT), &wasm);
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

    found = wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                         "memory", 6, &exported);
    assert(found);
    assert(exported.kind == WASMTIME_EXTERN_MEMORY);
    runtime->memory = exported.of.memory;
    wasm_math_set_memory(runtime->math_state, runtime->context, &runtime->memory);

    found = wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                         "run", 3, &exported);
    assert(found);
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    runtime->run = exported.of.func;
}

static void math_runtime_destroy(MathRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_math_binding_state_destroy(runtime->math_state);
    wasm_engine_delete(runtime->engine);
}

static void write_vec3(MathRuntime *runtime, int32_t offset, const float value[3])
{
    assert(offset >= 0);
    assert((size_t)offset + 3 * sizeof(float) <=
           wasmtime_memory_data_size(runtime->context, &runtime->memory));
    memcpy(wasmtime_memory_data(runtime->context, &runtime->memory) + offset,
           value, 3 * sizeof(float));
}

static void read_vec3(MathRuntime *runtime, int32_t offset, float value[3])
{
    assert(offset >= 0);
    assert((size_t)offset + 3 * sizeof(float) <=
           wasmtime_memory_data_size(runtime->context, &runtime->memory));
    memcpy(value,
           wasmtime_memory_data(runtime->context, &runtime->memory) + offset,
           3 * sizeof(float));
}

static void scale_vec3(MathRuntime *runtime, int32_t output_offset,
                       int32_t input_offset, float scale)
{
    wasmtime_val_t args[3];
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;

    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = output_offset;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = input_offset;
    args[2].kind = WASMTIME_F32;
    args[2].of.f32 = scale;
    error = wasmtime_func_call(runtime->context, &runtime->run,
                               args, 3, NULL, 0, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
}

static void assert_vec3(const float actual[3],
                        float expected_x, float expected_y, float expected_z)
{
    assert(fabsf(actual[0] - expected_x) < 0.0001f);
    assert(fabsf(actual[1] - expected_y) < 0.0001f);
    assert(fabsf(actual[2] - expected_z) < 0.0001f);
}

int main(void)
{
    static const int32_t INPUT_OFFSET = 0;
    static const int32_t OUTPUT_OFFSET = 32;
    const float input_a[3] = {1.0f, 2.0f, 3.0f};
    const float input_b[3] = {10.0f, 20.0f, 30.0f};
    const float untouched[3] = {-1.0f, -1.0f, -1.0f};
    float actual[3];
    MathRuntime runtime_a;
    MathRuntime runtime_b;

    math_runtime_create(&runtime_a);
    write_vec3(&runtime_a, INPUT_OFFSET, input_a);
    write_vec3(&runtime_a, OUTPUT_OFFSET, untouched);

    math_runtime_create(&runtime_b);
    write_vec3(&runtime_b, INPUT_OFFSET, input_b);
    write_vec3(&runtime_b, OUTPUT_OFFSET, untouched);

    scale_vec3(&runtime_a, OUTPUT_OFFSET, INPUT_OFFSET, 2.0f);
    read_vec3(&runtime_a, OUTPUT_OFFSET, actual);
    assert_vec3(actual, 2.0f, 4.0f, 6.0f);
    read_vec3(&runtime_b, OUTPUT_OFFSET, actual);
    assert_vec3(actual, -1.0f, -1.0f, -1.0f);

    scale_vec3(&runtime_b, OUTPUT_OFFSET, INPUT_OFFSET, 3.0f);
    read_vec3(&runtime_b, OUTPUT_OFFSET, actual);
    assert_vec3(actual, 30.0f, 60.0f, 90.0f);
    read_vec3(&runtime_a, OUTPUT_OFFSET, actual);
    assert_vec3(actual, 2.0f, 4.0f, 6.0f);

    math_runtime_destroy(&runtime_b);
    scale_vec3(&runtime_a, OUTPUT_OFFSET, INPUT_OFFSET, 4.0f);
    read_vec3(&runtime_a, OUTPUT_OFFSET, actual);
    assert_vec3(actual, 4.0f, 8.0f, 12.0f);
    math_runtime_destroy(&runtime_a);
    puts("WASM math binding state isolation test passed");
    return 0;
}