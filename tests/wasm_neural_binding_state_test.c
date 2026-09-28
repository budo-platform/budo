#include "neural/wasm_neural_bindings.h"
#include "tests/neural_mock.h"

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
    WasmNeuralBindingState *neural_state;
    wasmtime_instance_t instance;
    wasmtime_func_t load;
    wasmtime_func_t set_input;
    wasmtime_func_t run;
    wasmtime_func_t output;
} NeuralRuntime;

static const char NEURAL_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"neural_load_model\" (func $load (param i32 i32) (result i32)))\n"
    "  (import \"env\" \"neural_set_input_f32\" (func $set_input (param i32 i32 i32 i32 i32 i32 i32)))\n"
    "  (import \"env\" \"neural_run\" (func $run (param i32) (result i32)))\n"
    "  (import \"env\" \"neural_copy_output_f32\" (func $copy_output (param i32 i32 i32 i32 i32) (result i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (data (i32.const 0) \"model.onnx\")\n"
    "  (data (i32.const 32) \"input\")\n"
    "  (data (i32.const 48) \"output\")\n"
    "  (func (export \"load\") (result i32)\n"
    "    i32.const 0 i32.const 10 call $load)\n"
    "  (func (export \"set_input\") (param f32)\n"
    "    i32.const 64 local.get 0 f32.store\n"
    "    i32.const 0 i32.const 32 i32.const 5 i32.const 64 i32.const 1 i32.const 0 i32.const 0 call $set_input)\n"
    "  (func (export \"run\") (result i32)\n"
    "    i32.const 0 call $run)\n"
    "  (func (export \"output\") (result f32)\n"
    "    i32.const 0 i32.const 48 i32.const 6 i32.const 68 i32.const 1 call $copy_output drop\n"
    "    i32.const 68 f32.load))";

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

static wasmtime_func_t get_function(NeuralRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        name, strlen(name), &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void neural_runtime_create(NeuralRuntime *runtime, const char *project_dir)
{
    wasm_byte_vec_t wasm;
    wasmtime_extern_t memory;
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
    runtime->neural_state = wasm_neural_binding_state_create();
    assert(runtime->neural_state);
    assert(wasm_neural_binding_state_enable(runtime->neural_state, project_dir));

    error = wasm_neural_register(runtime->linker, runtime->neural_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(NEURAL_MODULE_WAT, strlen(NEURAL_MODULE_WAT), &wasm);
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
                                        "memory", 6, &memory));
    assert(memory.kind == WASMTIME_EXTERN_MEMORY);
    wasm_neural_binding_state_set_memory(runtime->neural_state,
                                         runtime->context, &memory.of.memory);
    runtime->load = get_function(runtime, "load");
    runtime->set_input = get_function(runtime, "set_input");
    runtime->run = get_function(runtime, "run");
    runtime->output = get_function(runtime, "output");
}

static void neural_runtime_destroy(NeuralRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_neural_binding_state_destroy(runtime->neural_state);
    wasm_engine_delete(runtime->engine);
}

static int32_t call_i32(NeuralRuntime *runtime, wasmtime_func_t *function)
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

static float call_f32(NeuralRuntime *runtime, wasmtime_func_t *function)
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

static void set_input(NeuralRuntime *runtime, float value)
{
    wasmtime_val_t argument;
    wasm_trap_t *trap = NULL;
    argument.kind = WASMTIME_F32;
    argument.of.f32 = value;
    wasmtime_error_t *error = wasmtime_func_call(
        runtime->context, &runtime->set_input, &argument, 1, NULL, 0, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
}

int main(void)
{
    NeuralRuntime runtime_a;
    NeuralRuntime runtime_b;
    const float expected_a = 10.0f + neural_mock_project_bias("wasm-a");
    const float expected_b = 20.0f + neural_mock_project_bias("wasm-b");

    neural_runtime_create(&runtime_a, "wasm-a");
    assert(call_i32(&runtime_a, &runtime_a.load) == 0);
    set_input(&runtime_a, 10.0f);

    neural_runtime_create(&runtime_b, "wasm-b");
    assert(call_i32(&runtime_b, &runtime_b.load) == 0);
    set_input(&runtime_b, 20.0f);
    assert(call_i32(&runtime_b, &runtime_b.run) == 1);
    assert(call_f32(&runtime_b, &runtime_b.output) == expected_b);

    assert(call_i32(&runtime_a, &runtime_a.run) == 1);
    assert(call_f32(&runtime_a, &runtime_a.output) == expected_a);
    assert(call_f32(&runtime_b, &runtime_b.output) == expected_b);

    neural_runtime_destroy(&runtime_b);
    set_input(&runtime_a, 30.0f);
    assert(call_i32(&runtime_a, &runtime_a.run) == 1);
    assert(call_f32(&runtime_a, &runtime_a.output) ==
           30.0f + neural_mock_project_bias("wasm-a"));
    neural_runtime_destroy(&runtime_a);

    puts("{\"runtime\":\"wasmtime\",\"operation\":\"neural.available\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"neural.loadModel\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"neural.instanceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");

    puts("WASM neural binding state isolation test passed");
    return 0;
}