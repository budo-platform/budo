#include "core/js_capabilities_bindings.h"
#include "core/lua_capabilities_bindings.h"
#include "core/wasm_capabilities_bindings.h"
#include "core/capabilities.h"
#include "core/subsystem_registry.h"
#include "device/js_device_bindings.h"
#include "device/lua_device_bindings.h"
#include "device/device_wrapper.h"
#include "device/wasm_device_bindings.h"
#include "math/js_math_bindings.h"
#include "math/lua_math_bindings.h"
#include "math/math_wrapper.h"
#include "math/wasm_math_bindings.h"

#include <assert.h>
#include <math.h>
#include <quickjs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasmtime.h>

#include <lauxlib.h>
#include <lualib.h>

#define TRACE_COUNT 9
#define TRACE_RESULT_CAPACITY 128
#define TRACE_ERROR_KIND_CAPACITY 32
#define TRACE_ERROR_CODE_CAPACITY API_ERROR_CODE_CAPACITY

typedef struct
{
    const char *operation;
    char result[TRACE_RESULT_CAPACITY];
    char error_kind[TRACE_ERROR_KIND_CAPACITY];
    char error_code[TRACE_ERROR_CODE_CAPACITY];
} TraceRecord;

typedef struct
{
    TraceRecord records[TRACE_COUNT];
} RuntimeTrace;

static const char *TRACE_OPERATIONS[TRACE_COUNT] = {
    "capabilities.neural",
    "capabilities.midi",
    "capabilities.udp",
    "capabilities.http",
    "capabilities.sensors",
    "device.keepScreenOn",
    "math.vec3Scale",
    "math.mat4Identity",
    "math.vec3Scale.invalidBuffer",
};

static void trace_init(RuntimeTrace *trace)
{
    memset(trace, 0, sizeof(*trace));
    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        trace->records[index].operation = TRACE_OPERATIONS[index];
        strcpy(trace->records[index].result, "null");
        strcpy(trace->records[index].error_kind, "none");
        trace->records[index].error_code[0] = '\0';
    }
}

static void trace_set_boolean(RuntimeTrace *trace, size_t index, bool value)
{
    strcpy(trace->records[index].result, value ? "true" : "false");
}

static void trace_set_literal(RuntimeTrace *trace, size_t index,
                              const char *value)
{
    assert(strlen(value) < sizeof(trace->records[index].result));
    strcpy(trace->records[index].result, value);
}

static void trace_set_error(RuntimeTrace *trace, size_t index,
                            const char *kind, const char *code)
{
    assert(strlen(kind) < sizeof(trace->records[index].error_kind));
    assert(strlen(code) < sizeof(trace->records[index].error_code));
    strcpy(trace->records[index].result, "null");
    strcpy(trace->records[index].error_kind, kind);
    strcpy(trace->records[index].error_code, code);
}

static void trace_set_vector(RuntimeTrace *trace, size_t index,
                             const float *values, size_t count)
{
    size_t used = 1;
    trace->records[index].result[0] = '[';
    trace->records[index].result[1] = '\0';
    for (size_t value_index = 0; value_index < count; value_index++)
    {
        int written = snprintf(trace->records[index].result + used,
                               sizeof(trace->records[index].result) - used,
                               "%s%.6g", value_index ? "," : "", values[value_index]);
        assert(written > 0);
        used += (size_t)written;
        assert(used < sizeof(trace->records[index].result));
    }
    assert(used + 1 < sizeof(trace->records[index].result));
    trace->records[index].result[used++] = ']';
    trace->records[index].result[used] = '\0';
}

static void trace_print_record(const char *runtime, const TraceRecord *record)
{
    printf("{\"runtime\":\"%s\",\"operation\":\"%s\","
           "\"result\":%s,\"errorKind\":\"%s\","
           "\"errorCode\":\"%s\"}\n",
           runtime, record->operation, record->result,
           record->error_kind, record->error_code);
}

static void trace_compare(const char *expected_runtime,
                          const RuntimeTrace *expected,
                          const char *actual_runtime,
                          const RuntimeTrace *actual)
{
    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        const TraceRecord *expected_record = &expected->records[index];
        const TraceRecord *actual_record = &actual->records[index];
        if (strcmp(expected_record->operation, actual_record->operation) != 0 ||
            strcmp(expected_record->result, actual_record->result) != 0 ||
            strcmp(expected_record->error_kind, actual_record->error_kind) != 0 ||
            strcmp(expected_record->error_code, actual_record->error_code) != 0)
        {
            fprintf(stderr, "Conformance mismatch between %s and %s:\n",
                    expected_runtime, actual_runtime);
            trace_print_record(expected_runtime, expected_record);
            trace_print_record(actual_runtime, actual_record);
            abort();
        }
    }
}

static void js_fail_exception(JSContext *context, const char *label)
{
    JSValue exception = JS_GetException(context);
    const char *message = JS_ToCString(context, exception);
    fprintf(stderr, "%s: %s\n", label, message ? message : "unknown QuickJS error");
    JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    abort();
}

static int js_get_global_integer(JSContext *context, const char *name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    int32_t result = 0;
    if (JS_IsException(value) || JS_ToInt32(context, &result, value) < 0)
        js_fail_exception(context, name);
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return result;
}

static float js_get_global_number(JSContext *context, const char *name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    double result = 0.0;
    if (JS_IsException(value) || JS_ToFloat64(context, &result, value) < 0)
        js_fail_exception(context, name);
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return (float)result;
}

static void run_javascript_trace(RuntimeTrace *trace)
{
    static const char script[] =
        "const c = sys.capabilities;\n"
        "globalThis.capMask ="
        " (c.neural.available ? 1 : 0) |"
        " (c.midi.available ? 2 : 0) |"
        " (c.udp.available ? 4 : 0) |"
        " (c.http.available ? 8 : 0) |"
        " (c.sensors.available ? 16 : 0);\n"
        "globalThis.deviceResult ="
        " (sys.device.keepScreenOn(true) ? 1 : 0) |"
        " (sys.device.keepScreenOn(false) ? 2 : 0);\n"
        "const input = new Float32Array([1, 2, 3]);\n"
        "const scaled = new Float32Array(3);\n"
        "sys.math.vec3Scale(scaled, input, 2);\n"
        "globalThis.vecX = scaled[0]; globalThis.vecY = scaled[1];"
        " globalThis.vecZ = scaled[2];\n"
        "const identity = new Float32Array(16);\n"
        "sys.math.mat4Identity(identity);\n"
        "globalThis.identityDiag = identity[0] + identity[5] +"
        " identity[10] + identity[15];\n"
        "try { sys.math.vec3Scale(new Float32Array(3),"
        " new Float32Array(2), 2); }"
        " catch (error) { globalThis.invalidBufferCaught ="
        " String(error).includes('math.invalid_input_buffer'); }\n";

    JSRuntime *runtime = JS_NewRuntime();
    assert(runtime);
    JSContext *context = JS_NewContext(runtime);
    assert(context);
    JSValue global = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, global, "sys", JS_NewObject(context));
    JS_FreeValue(context, global);

    js_capabilities_init(context);
    DeviceContext *device_ctx = js_device_init(context);
    assert(device_ctx);
    js_math_init(context);

    JSValue evaluated = JS_Eval(context, script, strlen(script),
                                "basic-conformance.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(evaluated))
        js_fail_exception(context, "JavaScript conformance script");
    JS_FreeValue(context, evaluated);

    trace_init(trace);
    int capability_mask = js_get_global_integer(context, "capMask");
    for (size_t index = 0; index < 5; index++)
        trace_set_boolean(trace, index, (capability_mask & (1 << index)) != 0);
    assert(js_get_global_integer(context, "deviceResult") == 3);
    trace_set_literal(trace, 5, "[true,true]");
    float vector[3] = {
        js_get_global_number(context, "vecX"),
        js_get_global_number(context, "vecY"),
        js_get_global_number(context, "vecZ"),
    };
    trace_set_vector(trace, 6, vector, 3);
    float diagonal = js_get_global_number(context, "identityDiag");
    trace_set_vector(trace, 7, &diagonal, 1);
    assert(js_get_global_integer(context, "invalidBufferCaught") == 1);
    trace_set_error(trace, 8, "programmer", "math.invalid_input_buffer");

    assert(device_binding_state_owns_screen_request(device_ctx) == false);
    assert(device_binding_state_keep_screen_on(device_ctx, true, NULL));
    assert(device_is_screen_kept_on());
    device_binding_state_destroy(device_ctx);
    assert(!device_is_screen_kept_on());
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

static double lua_get_global_number(lua_State *state, const char *name)
{
    lua_getglobal(state, name);
    assert(lua_isnumber(state, -1));
    double value = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return value;
}

static void run_lua_trace(RuntimeTrace *trace)
{
    static const char script[] =
        "local c = sys.capabilities\n"
        "capMask ="
        " (c.neural.available and 1 or 0) +"
        " (c.midi.available and 2 or 0) +"
        " (c.udp.available and 4 or 0) +"
        " (c.http.available and 8 or 0) +"
        " (c.sensors.available and 16 or 0)\n"
        "deviceResult ="
        " (sys.device.keepScreenOn(true) and 1 or 0) +"
        " (sys.device.keepScreenOn(false) and 2 or 0)\n"
        "local scaled = {0, 0, 0}\n"
        "sys.math.vec3Scale(scaled, {1, 2, 3}, 2)\n"
        "vecX, vecY, vecZ = scaled[1], scaled[2], scaled[3]\n"
        "local identity = {}\n"
        "sys.math.mat4Identity(identity)\n"
        "identityDiag = identity[1] + identity[6] + identity[11] + identity[16]\n"
        "local ok, message = pcall(sys.math.vec3Scale, {0, 0, 0}, {1, 2}, 2)\n"
        "invalidBufferCaught = (not ok) and"
        " string.find(message, 'math.invalid_input_buffer', 1, true) ~= nil\n";

    lua_State *state = luaL_newstate();
    assert(state);
    luaL_openlibs(state);
    lua_newtable(state);
    lua_setglobal(state, "sys");

    lua_capabilities_init(state);
    DeviceContext *device_ctx = lua_device_init(state);
    assert(device_ctx);
    lua_math_init(state);

    if (luaL_dostring(state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua conformance script: %s\n", lua_tostring(state, -1));
        abort();
    }

    trace_init(trace);
    int capability_mask = (int)lua_get_global_number(state, "capMask");
    for (size_t index = 0; index < 5; index++)
        trace_set_boolean(trace, index, (capability_mask & (1 << index)) != 0);
    assert((int)lua_get_global_number(state, "deviceResult") == 3);
    trace_set_literal(trace, 5, "[true,true]");
    float vector[3] = {
        (float)lua_get_global_number(state, "vecX"),
        (float)lua_get_global_number(state, "vecY"),
        (float)lua_get_global_number(state, "vecZ"),
    };
    trace_set_vector(trace, 6, vector, 3);
    float diagonal = (float)lua_get_global_number(state, "identityDiag");
    trace_set_vector(trace, 7, &diagonal, 1);
    lua_getglobal(state, "invalidBufferCaught");
    assert(lua_toboolean(state, -1));
    lua_pop(state, 1);
    trace_set_error(trace, 8, "programmer", "math.invalid_input_buffer");

    assert(device_binding_state_keep_screen_on(device_ctx, true, NULL));
    assert(device_is_screen_kept_on());
    device_binding_state_destroy(device_ctx);
    assert(!device_is_screen_kept_on());
    lua_close(state);
}

typedef struct
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    DeviceContext *device_ctx;
    WasmMathBindingState *math_state;
    wasmtime_instance_t instance;
} WasmRuntime;

static const char WASM_CONFORMANCE_MODULE[] =
    "(module\n"
    "  (import \"env\" \"capability_neural\" (func $cn (result i32)))\n"
    "  (import \"env\" \"capability_midi\" (func $cm (result i32)))\n"
    "  (import \"env\" \"capability_udp\" (func $cu (result i32)))\n"
    "  (import \"env\" \"capability_http\" (func $ch (result i32)))\n"
    "  (import \"env\" \"capability_sensors\" (func $cs (result i32)))\n"
    "  (import \"env\" \"device_keep_screen_on\" (func $device (param i32) (result i32)))\n"
    "  (import \"env\" \"math_vec3_scale\" (func $scale (param i32 i32 f32)))\n"
    "  (import \"env\" \"math_mat4_identity\" (func $identity (param i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (func (export \"capabilities\") (result i32)\n"
    "    call $cn\n"
    "    call $cm i32.const 1 i32.shl i32.or\n"
    "    call $cu i32.const 2 i32.shl i32.or\n"
    "    call $ch i32.const 3 i32.shl i32.or\n"
    "    call $cs i32.const 4 i32.shl i32.or)\n"
    "  (func (export \"device\") (result i32)\n"
    "    i32.const 1 call $device\n"
    "    i32.const 0 call $device i32.const 1 i32.shl i32.or)\n"
    "  (func (export \"math\")\n"
    "    i32.const 0 f32.const 1 f32.store\n"
    "    i32.const 4 f32.const 2 f32.store\n"
    "    i32.const 8 f32.const 3 f32.store\n"
    "    i32.const 16 i32.const 0 f32.const 2 call $scale\n"
    "    i32.const 32 call $identity)\n"
    "  (func (export \"invalid\")\n"
    "    i32.const 16 i32.const 65532 f32.const 2 call $scale)\n"
    "  (func (export \"load\") (param i32) (result f32)\n"
    "    local.get 0 f32.load))";

static void fail_wasmtime_error(wasmtime_error_t *error)
{
    wasm_byte_vec_t message;
    wasmtime_error_message(error, &message);
    fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    abort();
}

static void fail_wasm_trap(wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    wasm_trap_message(trap, &message);
    fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
    abort();
}

static wasmtime_func_t wasm_get_function(WasmRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    bool found = wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                              name, strlen(name), &exported);
    assert(found);
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void wasm_call(WasmRuntime *runtime, wasmtime_func_t *function,
                      const wasmtime_val_t *arguments, size_t argument_count,
                      wasmtime_val_t *results, size_t result_count)
{
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(
        runtime->context, function, arguments, argument_count,
        results, result_count, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
}

static int wasm_call_integer(WasmRuntime *runtime, wasmtime_func_t *function)
{
    wasmtime_val_t result;
    wasm_call(runtime, function, NULL, 0, &result, 1);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static float wasm_load_float(WasmRuntime *runtime, wasmtime_func_t *load,
                             int32_t offset)
{
    wasmtime_val_t argument;
    wasmtime_val_t result;
    argument.kind = WASMTIME_I32;
    argument.of.i32 = offset;
    wasm_call(runtime, load, &argument, 1, &result, 1);
    assert(result.kind == WASMTIME_F32);
    return result.of.f32;
}

static void run_wasmtime_trace(RuntimeTrace *trace)
{
    WasmRuntime runtime;
    wasm_byte_vec_t wasm;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;
    memset(&runtime, 0, sizeof(runtime));

    runtime.engine = wasm_engine_new();
    assert(runtime.engine);
    runtime.store = wasmtime_store_new(runtime.engine, NULL, NULL);
    assert(runtime.store);
    runtime.context = wasmtime_store_context(runtime.store);
    runtime.linker = wasmtime_linker_new(runtime.engine);
    assert(runtime.linker);
    runtime.math_state = wasm_math_binding_state_create();
    assert(runtime.math_state);
    runtime.device_ctx = device_binding_state_create();
    assert(runtime.device_ctx);

    error = wasm_capabilities_register(runtime.linker);
    if (error)
        fail_wasmtime_error(error);
    error = wasm_device_register(runtime.linker, runtime.device_ctx);
    if (error)
        fail_wasmtime_error(error);
    error = wasm_math_register(runtime.linker, runtime.math_state);
    if (error)
        fail_wasmtime_error(error);

    error = wasmtime_wat2wasm(WASM_CONFORMANCE_MODULE,
                              strlen(WASM_CONFORMANCE_MODULE), &wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_module_new(runtime.engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime.module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_linker_instantiate(runtime.linker, runtime.context,
                                        runtime.module, &runtime.instance, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);

    wasmtime_extern_t memory_export;
    bool found = wasmtime_instance_export_get(runtime.context, &runtime.instance,
                                              "memory", 6, &memory_export);
    assert(found);
    assert(memory_export.kind == WASMTIME_EXTERN_MEMORY);
    wasm_math_set_memory(runtime.math_state, runtime.context,
                         &memory_export.of.memory);

    wasmtime_func_t capabilities = wasm_get_function(&runtime, "capabilities");
    wasmtime_func_t device = wasm_get_function(&runtime, "device");
    wasmtime_func_t math = wasm_get_function(&runtime, "math");
    wasmtime_func_t load = wasm_get_function(&runtime, "load");
    wasmtime_func_t invalid = wasm_get_function(&runtime, "invalid");

    trace_init(trace);
    int capability_mask = wasm_call_integer(&runtime, &capabilities);
    for (size_t index = 0; index < 5; index++)
        trace_set_boolean(trace, index, (capability_mask & (1 << index)) != 0);
    assert(wasm_call_integer(&runtime, &device) == 3);
    trace_set_literal(trace, 5, "[true,true]");
    wasm_call(&runtime, &math, NULL, 0, NULL, 0);
    float vector[3] = {
        wasm_load_float(&runtime, &load, 16),
        wasm_load_float(&runtime, &load, 20),
        wasm_load_float(&runtime, &load, 24),
    };
    trace_set_vector(trace, 6, vector, 3);
    float diagonal = wasm_load_float(&runtime, &load, 32) +
                     wasm_load_float(&runtime, &load, 52) +
                     wasm_load_float(&runtime, &load, 72) +
                     wasm_load_float(&runtime, &load, 92);
    trace_set_vector(trace, 7, &diagonal, 1);
    wasm_call(&runtime, &invalid, NULL, 0, NULL, 0);
    const ApiError *math_error = wasm_math_binding_state_error(runtime.math_state);
    assert(math_error && math_error->status == API_STATUS_INVALID_ARGUMENT);
    trace_set_error(trace, 8, "programmer", math_error->code);

    assert(device_binding_state_keep_screen_on(runtime.device_ctx, true, NULL));
    assert(device_is_screen_kept_on());
    device_binding_state_destroy(runtime.device_ctx);
    assert(!device_is_screen_kept_on());
    wasmtime_module_delete(runtime.module);
    wasmtime_linker_delete(runtime.linker);
    wasmtime_store_delete(runtime.store);
    wasm_math_binding_state_destroy(runtime.math_state);
    wasm_engine_delete(runtime.engine);
}

static void test_checked_services(void)
{
    ApiError error;
    CapabilitySnapshot snapshot;
    SubsystemRegistry registry;
    DeviceContext *device_ctx;
    DeviceContext *other_device_ctx;
    float matrix[16] = {0};
    float output[16];

    assert(!capabilities_get_snapshot(NULL, &error));
    assert(error.status == API_STATUS_INVALID_ARGUMENT);
    assert(strcmp(error.code, "capabilities.invalid_snapshot") == 0);
    assert(capabilities_get_snapshot(&snapshot, &error));
    assert(!api_error_has_error(&error));

    assert(!math_service_mat4_identity(output, 15, &error));
    assert(error.status == API_STATUS_INVALID_ARGUMENT);
    assert(strcmp(error.code, "math.invalid_output_buffer") == 0);
    assert(math_service_mat4_identity(output, 16, &error));
    assert(!api_error_has_error(&error));

    assert(!math_service_mat4_invert(output, 16, matrix, 16, &error));
    assert(error.status == API_STATUS_APPLICATION_ERROR);
    assert(strcmp(error.code, "math.singular_matrix") == 0);

    subsystem_registry_init(&registry);
    device_ctx = device_binding_state_create();
    assert(device_ctx);
    assert(subsystem_registry_register(
        &registry, "test device", device_ctx, NULL,
        (SubsystemCleanupCallback)device_binding_state_destroy));
    assert(device_binding_state_keep_screen_on(device_ctx, true, &error));
    assert(device_is_screen_kept_on());
    subsystem_registry_shutdown(&registry);
    assert(!device_is_screen_kept_on());

    device_ctx = device_binding_state_create();
    other_device_ctx = device_binding_state_create();
    assert(device_ctx && other_device_ctx);
    assert(device_binding_state_keep_screen_on(device_ctx, true, &error));
    assert(device_binding_state_keep_screen_on(other_device_ctx, true, &error));
    device_binding_state_destroy(device_ctx);
    assert(device_is_screen_kept_on());
    assert(device_binding_state_owns_screen_request(other_device_ctx));
    device_binding_state_destroy(other_device_ctx);
    assert(!device_is_screen_kept_on());
}

int main(void)
{
    RuntimeTrace javascript;
    RuntimeTrace lua;
    RuntimeTrace wasmtime;

    test_checked_services();
    run_javascript_trace(&javascript);
    run_lua_trace(&lua);
    run_wasmtime_trace(&wasmtime);

    trace_compare("javascript", &javascript, "lua", &lua);
    trace_compare("javascript", &javascript, "wasmtime", &wasmtime);

    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        trace_print_record("javascript", &javascript.records[index]);
        trace_print_record("lua", &lua.records[index]);
        trace_print_record("wasmtime", &wasmtime.records[index]);
    }
    puts("basic runtime conformance tests passed");
    return 0;
}