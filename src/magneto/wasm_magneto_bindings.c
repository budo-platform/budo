#include "wasm_magneto_bindings.h"
#include "magneto_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WasmMagnetoBindingState
{
    MagnetoContext *magneto_ctx;
};

static MagnetoContext *wasm_magneto_context(void *env)
{
    WasmMagnetoBindingState *state = (WasmMagnetoBindingState *)env;
    return state ? state->magneto_ctx : NULL;
}

static wasmtime_error_t *define_magneto_func(
    wasmtime_linker_t *linker,
    WasmMagnetoBindingState *state,
    const char *name,
    wasmtime_func_callback_t callback,
    const wasm_valkind_t *param_types, size_t param_count,
    const wasm_valkind_t *result_types, size_t result_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t index = 0; index < param_count; index++)
        params.data[index] = wasm_valtype_new(param_types[index]);
    wasm_valtype_vec_new_uninitialized(&results, result_count);
    for (size_t index = 0; index < result_count; index++)
        results.data[index] = wasm_valtype_new(result_types[index]);

    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name), functype, callback, state, NULL);
    wasm_functype_delete(functype);
    return error;
}

static wasm_trap_t *host_magneto_is_available(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = (magneto_ctx && magneto_is_available(magneto_ctx)) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_magneto_has_accelerometer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = (magneto_ctx && magneto_has_accelerometer(magneto_ctx)) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_magneto_has_compass(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = (magneto_ctx && magneto_has_compass(magneto_ctx)) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_magneto_start(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    results[0].kind = WASMTIME_I32;
    ApiError error;
    results[0].of.i32 = magneto_service_start(magneto_ctx, &error) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_magneto_stop(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (magneto_ctx)
        magneto_stop(magneto_ctx);
    return NULL;
}

static wasm_trap_t *host_magneto_is_active(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = (magneto_ctx && magneto_is_active(magneto_ctx)) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_magneto_get_accel_x(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoAccelData data = {0};
    if (magneto_ctx)
        magneto_get_accel(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.x;
    return NULL;
}

static wasm_trap_t *host_magneto_get_accel_y(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoAccelData data = {0};
    if (magneto_ctx)
        magneto_get_accel(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.y;
    return NULL;
}

static wasm_trap_t *host_magneto_get_accel_z(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoAccelData data = {0};
    if (magneto_ctx)
        magneto_get_accel(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.z;
    return NULL;
}

static wasm_trap_t *host_magneto_get_compass_x(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoCompassData data = {0};
    if (magneto_ctx)
        magneto_get_compass(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.x;
    return NULL;
}

static wasm_trap_t *host_magneto_get_compass_y(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoCompassData data = {0};
    if (magneto_ctx)
        magneto_get_compass(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.y;
    return NULL;
}

static wasm_trap_t *host_magneto_get_compass_z(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoCompassData data = {0};
    if (magneto_ctx)
        magneto_get_compass(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.z;
    return NULL;
}

static wasm_trap_t *host_magneto_get_compass_heading(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    MagnetoContext *magneto_ctx = wasm_magneto_context(env);
    (void)caller;
    (void)args;
    (void)nargs;
    MagnetoCompassData data = {0};
    if (magneto_ctx)
        magneto_get_compass(magneto_ctx, &data);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = data.heading;
    return NULL;
}

wasmtime_error_t *wasm_magneto_register(wasmtime_linker_t *linker,
                                        WasmMagnetoBindingState *state)
{
    wasmtime_error_t *error;

    if (!linker || !state || !state->magneto_ctx)
        return wasmtime_error_new("WASM magneto binding state is required");

    wasm_valkind_t no_params[] = {0};
    wasm_valkind_t i32_result[] = {WASM_I32};
    wasm_valkind_t f32_result[] = {WASM_F32};

    error = define_magneto_func(linker, state, "magneto_is_available",
                                host_magneto_is_available, no_params, 0, i32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_has_accelerometer",
                                host_magneto_has_accelerometer, no_params, 0, i32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_has_compass",
                                host_magneto_has_compass, no_params, 0, i32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_start",
                                host_magneto_start, no_params, 0, i32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_stop",
                                host_magneto_stop, no_params, 0, NULL, 0);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_is_active",
                                host_magneto_is_active, no_params, 0, i32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_accel_x",
                                host_magneto_get_accel_x, no_params, 0, f32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_accel_y",
                                host_magneto_get_accel_y, no_params, 0, f32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_accel_z",
                                host_magneto_get_accel_z, no_params, 0, f32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_compass_x",
                                host_magneto_get_compass_x, no_params, 0, f32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_compass_y",
                                host_magneto_get_compass_y, no_params, 0, f32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_compass_z",
                                host_magneto_get_compass_z, no_params, 0, f32_result, 1);
    if (error)
        return error;

    error = define_magneto_func(linker, state, "magneto_get_compass_heading",
                                host_magneto_get_compass_heading, no_params, 0, f32_result, 1);
    if (error)
        return error;

    return NULL;
}

WasmMagnetoBindingState *wasm_magneto_binding_state_create(void)
{
    WasmMagnetoBindingState *state =
        (WasmMagnetoBindingState *)calloc(1, sizeof(WasmMagnetoBindingState));
    if (!state)
        return NULL;

    state->magneto_ctx = magneto_create();
    if (!state->magneto_ctx)
    {
        fprintf(stderr, "Failed to create WASM magneto context\n");
        free(state);
        return NULL;
    }
    return state;
}

void wasm_magneto_binding_state_destroy(WasmMagnetoBindingState *state)
{
    if (!state)
        return;
    magneto_destroy(state->magneto_ctx);
    free(state);
}