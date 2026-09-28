#include "audio/js_audio_bindings.h"
#include "audio/lua_audio_bindings.h"
#include "audio/wasm_audio_bindings.h"
#include "magneto/js_magneto_bindings.h"
#include "magneto/lua_magneto_bindings.h"
#include "magneto/wasm_magneto_bindings.h"
#include "tests/audio_mock.h"
#include "tests/magneto_mock.h"
#include "tests/runtime_trace.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wasmtime.h>

#include <lauxlib.h>
#include <lualib.h>
#include <quickjs.h>

#define TRACE_COUNT 5

static const char *OPERATIONS[TRACE_COUNT] = {
    "magneto.available",
    "magneto.start",
    "magneto.accelShape",
    "magneto.stop",
    "audio.createOscillator",
};

static void js_fail(JSContext *context)
{
    JSValue exception = JS_GetException(context);
    const char *message = JS_ToCString(context, exception);
    fprintf(stderr, "JavaScript conformance error: %s\n",
            message ? message : "unknown");
    JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    abort();
}

static bool js_bool(JSContext *context, const char *name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    bool result = JS_ToBool(context, value) == 1;
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return result;
}

static void run_javascript(RuntimeTrace *trace, RuntimeTraceRecord *records)
{
    static const char script[] =
        "globalThis.magAvailable = sys.sensors.isAvailable();\n"
        "globalThis.magStarted = sys.sensors.start() && sys.sensors.isActive();\n"
        "const a = sys.sensors.getAccel();\n"
        "globalThis.magShape = !!a && a.y === a.x + 1 && a.z === a.x + 2;\n"
        "sys.sensors.stop(); globalThis.magStopped = !sys.sensors.isActive();\n"
        "globalThis.audioCreated = sys.audio.createOscillator() >= 0;\n";
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = JS_NewContext(runtime);
    JSValue global = JS_GetGlobalObject(context);
    MagnetoContext *magneto;
    JsAudioContext *audio;
    JSValue result;

    assert(runtime && context);
    JS_SetPropertyStr(context, global, "sys", JS_NewObject(context));
    JS_FreeValue(context, global);
    magneto = js_magneto_init(context);
    audio = js_audio_init(context, "shared-audio-root");
    assert(magneto && audio);
    magneto_mock_set_value(magneto, 11.0f);
    audio_mock_reset();
    result = JS_Eval(context, script, strlen(script), "conformance.js",
                     JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
        js_fail(context);
    JS_FreeValue(context, result);

    runtime_trace_init(trace, records, OPERATIONS, TRACE_COUNT);
    runtime_trace_result(trace, 0, js_bool(context, "magAvailable") ? "true" : "false");
    runtime_trace_result(trace, 1, js_bool(context, "magStarted") ? "true" : "false");
    runtime_trace_result(trace, 2, js_bool(context, "magShape") ? "true" : "false");
    runtime_trace_result(trace, 3, js_bool(context, "magStopped") ? "true" : "false");
    runtime_trace_result(trace, 4, js_bool(context, "audioCreated") ? "true" : "false");
    assert(audio_mock_create_count() == 1);

    js_audio_cleanup(audio);
    js_magneto_cleanup(magneto);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    assert(audio_mock_live_count() == 0);
}

static bool lua_bool(lua_State *state, const char *name)
{
    lua_getglobal(state, name);
    bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

static void run_lua(RuntimeTrace *trace, RuntimeTraceRecord *records)
{
    static const char script[] =
        "mag_available = sys.sensors.isAvailable()\n"
        "mag_started = sys.sensors.start() and sys.sensors.isActive()\n"
        "local a = sys.sensors.getAccel()\n"
        "mag_shape = a ~= nil and a.y == a.x + 1 and a.z == a.x + 2\n"
        "sys.sensors.stop(); mag_stopped = not sys.sensors.isActive()\n"
        "audio_created = sys.audio.createOscillator() >= 0\n";
    lua_State *state = luaL_newstate();
    MagnetoContext *magneto;
    LuaAudioContext *audio;

    assert(state);
    luaL_openlibs(state);
    lua_newtable(state);
    lua_setglobal(state, "sys");
    magneto = lua_magneto_init(state);
    audio = lua_audio_init(state, "shared-audio-root");
    assert(magneto && audio);
    magneto_mock_set_value(magneto, 21.0f);
    audio_mock_reset();
    if (luaL_dostring(state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua conformance error: %s\n", lua_tostring(state, -1));
        abort();
    }

    runtime_trace_init(trace, records, OPERATIONS, TRACE_COUNT);
    runtime_trace_result(trace, 0, lua_bool(state, "mag_available") ? "true" : "false");
    runtime_trace_result(trace, 1, lua_bool(state, "mag_started") ? "true" : "false");
    runtime_trace_result(trace, 2, lua_bool(state, "mag_shape") ? "true" : "false");
    runtime_trace_result(trace, 3, lua_bool(state, "mag_stopped") ? "true" : "false");
    runtime_trace_result(trace, 4, lua_bool(state, "audio_created") ? "true" : "false");
    assert(audio_mock_create_count() == 1);

    lua_audio_cleanup(audio);
    lua_magneto_cleanup(magneto);
    lua_close(state);
    assert(audio_mock_live_count() == 0);
}

typedef struct WasmRuntime
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmMagnetoBindingState *magneto;
    WasmAudioBindingState *audio;
    wasmtime_instance_t instance;
} WasmRuntime;

static const char WAT[] =
    "(module\n"
    " (import \"env\" \"magneto_is_available\" (func $available (result i32)))\n"
    " (import \"env\" \"magneto_start\" (func $start (result i32)))\n"
    " (import \"env\" \"magneto_stop\" (func $stop))\n"
    " (import \"env\" \"magneto_is_active\" (func $active (result i32)))\n"
    " (import \"env\" \"magneto_get_accel_x\" (func $x (result f32)))\n"
    " (import \"env\" \"magneto_get_accel_y\" (func $y (result f32)))\n"
    " (import \"env\" \"magneto_get_accel_z\" (func $z (result f32)))\n"
    " (import \"env\" \"audio_create_oscillator\" (func $osc (result i32)))\n"
    " (export \"available\" (func $available))\n"
    " (export \"start\" (func $start))\n"
    " (export \"stop\" (func $stop))\n"
    " (export \"active\" (func $active))\n"
    " (export \"x\" (func $x))\n"
    " (export \"y\" (func $y))\n"
    " (export \"z\" (func $z))\n"
    " (export \"osc\" (func $osc)))";

static void wasm_fail(wasmtime_error_t *error, wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    if (error)
    {
        wasmtime_error_message(error, &message);
        fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
        wasmtime_error_delete(error);
    }
    else
    {
        wasm_trap_message(trap, &message);
        fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
        wasm_trap_delete(trap);
    }
    wasm_byte_vec_delete(&message);
    abort();
}

static wasmtime_func_t wasm_function(WasmRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    bool found = wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                              name, strlen(name), &exported);
    assert(found && exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static int32_t wasm_i32(WasmRuntime *runtime, wasmtime_func_t *function)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 NULL, 0, &result, 1, &trap);
    if (error || trap)
        wasm_fail(error, trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static float wasm_f32(WasmRuntime *runtime, wasmtime_func_t *function)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 NULL, 0, &result, 1, &trap);
    if (error || trap)
        wasm_fail(error, trap);
    assert(result.kind == WASMTIME_F32);
    return result.of.f32;
}

static void wasm_void(WasmRuntime *runtime, wasmtime_func_t *function)
{
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 NULL, 0, NULL, 0, &trap);
    if (error || trap)
        wasm_fail(error, trap);
}

static void run_wasmtime(RuntimeTrace *trace, RuntimeTraceRecord *records)
{
    WasmRuntime runtime;
    wasm_byte_vec_t wasm;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;
    wasmtime_func_t available, start, stop, active, x, y, z, osc;
    float ax, ay, az;

    memset(&runtime, 0, sizeof(runtime));
    runtime.engine = wasm_engine_new();
    runtime.store = wasmtime_store_new(runtime.engine, NULL, NULL);
    runtime.context = wasmtime_store_context(runtime.store);
    runtime.linker = wasmtime_linker_new(runtime.engine);
    runtime.magneto = wasm_magneto_binding_state_create();
    runtime.audio = wasm_audio_binding_state_create("shared-audio-root");
    assert(runtime.engine && runtime.store && runtime.linker && runtime.magneto && runtime.audio);
    error = wasm_magneto_register(runtime.linker, runtime.magneto);
    if (error)
        wasm_fail(error, NULL);
    error = wasm_audio_register(runtime.linker, runtime.audio);
    if (error)
        wasm_fail(error, NULL);
    error = wasmtime_wat2wasm(WAT, strlen(WAT), &wasm);
    if (error)
        wasm_fail(error, NULL);
    error = wasmtime_module_new(runtime.engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime.module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        wasm_fail(error, NULL);
    error = wasmtime_linker_instantiate(runtime.linker, runtime.context,
                                        runtime.module, &runtime.instance, &trap);
    if (error || trap)
        wasm_fail(error, trap);
    available = wasm_function(&runtime, "available");
    start = wasm_function(&runtime, "start");
    stop = wasm_function(&runtime, "stop");
    active = wasm_function(&runtime, "active");
    x = wasm_function(&runtime, "x");
    y = wasm_function(&runtime, "y");
    z = wasm_function(&runtime, "z");
    osc = wasm_function(&runtime, "osc");

    audio_mock_reset();
    runtime_trace_init(trace, records, OPERATIONS, TRACE_COUNT);
    runtime_trace_result(trace, 0, wasm_i32(&runtime, &available) ? "true" : "false");
    assert(wasm_i32(&runtime, &start) == 1 && wasm_i32(&runtime, &active) == 1);
    runtime_trace_result(trace, 1, "true");
    ax = wasm_f32(&runtime, &x);
    ay = wasm_f32(&runtime, &y);
    az = wasm_f32(&runtime, &z);
    runtime_trace_result(trace, 2, (ay == ax + 1.0f && az == ax + 2.0f) ? "true" : "false");
    wasm_void(&runtime, &stop);
    runtime_trace_result(trace, 3, wasm_i32(&runtime, &active) == 0 ? "true" : "false");
    runtime_trace_result(trace, 4, wasm_i32(&runtime, &osc) >= 0 ? "true" : "false");
    assert(audio_mock_create_count() == 1);

    wasmtime_module_delete(runtime.module);
    wasmtime_linker_delete(runtime.linker);
    wasmtime_store_delete(runtime.store);
    wasm_audio_binding_state_destroy(runtime.audio);
    wasm_magneto_binding_state_destroy(runtime.magneto);
    wasm_engine_delete(runtime.engine);
    assert(audio_mock_live_count() == 0);
}

int main(void)
{
    RuntimeTrace javascript, lua, wasmtime;
    RuntimeTraceRecord js_records[TRACE_COUNT];
    RuntimeTraceRecord lua_records[TRACE_COUNT];
    RuntimeTraceRecord wasm_records[TRACE_COUNT];

    run_javascript(&javascript, js_records);
    run_lua(&lua, lua_records);
    run_wasmtime(&wasmtime, wasm_records);
    runtime_trace_compare("javascript", &javascript, "lua", &lua);
    runtime_trace_compare("javascript", &javascript, "wasmtime", &wasmtime);
    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        runtime_trace_print("javascript", &js_records[index]);
        runtime_trace_print("lua", &lua_records[index]);
        runtime_trace_print("wasmtime", &wasm_records[index]);
    }
    puts("Magneto/audio runtime conformance tests passed");
    return 0;
}