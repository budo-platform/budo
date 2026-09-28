#include "wasm_math_bindings.h"
#include "math/math_wrapper.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WasmMathBindingState
{
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool has_memory;
    ApiError error;
};

static void *wasm_math_get(WasmMathBindingState *state, int32_t ptr, int float_count)
{
    uint8_t *mem;
    size_t mem_size;
    size_t bytes = (size_t)float_count * sizeof(float);
    if (!state || !state->store_ctx || !state->has_memory || ptr < 0)
        return NULL;
    mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);
    if ((size_t)ptr + bytes > mem_size)
        return NULL;
    return mem + ptr;
}

static void wasm_math_report_error(const ApiError *error)
{
    if (api_error_has_error(error))
        fprintf(stderr, "%s: %s\n", error->code, error->message);
}

static void wasm_math_store_error(WasmMathBindingState *state,
                                  const ApiError *error)
{
    if (!state)
        return;
    if (error)
        state->error = *error;
    else
        api_error_clear(&state->error);
}

#define HOST_PROLOGUE                                          \
    WasmMathBindingState *state = (WasmMathBindingState *)env; \
    (void)caller;                                              \
    (void)nargs;                                               \
    (void)nresults

static wasm_trap_t *host_mat4_identity(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    ApiError error;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    if (!math_service_mat4_identity(o, o ? 16 : 0, &error))
        wasm_math_report_error(&error);
    wasm_math_store_error(state, &error);
    return NULL;
}

static wasm_trap_t *host_mat4_multiply(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 16);
    float *b = (float *)wasm_math_get(state, args[2].of.i32, 16);
    if (o && a && b)
        math_mat4_multiply(o, a, b);
    return NULL;
}

static wasm_trap_t *host_mat4_perspective(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    if (o)
        math_mat4_perspective(o, args[1].of.f32, args[2].of.f32,
                              args[3].of.f32, args[4].of.f32);
    return NULL;
}

static wasm_trap_t *host_mat4_ortho(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    if (o)
        math_mat4_ortho(o, args[1].of.f32, args[2].of.f32, args[3].of.f32,
                        args[4].of.f32, args[5].of.f32, args[6].of.f32);
    return NULL;
}

static wasm_trap_t *host_mat4_lookat(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *e = (float *)wasm_math_get(state, args[1].of.i32, 3);
    float *t = (float *)wasm_math_get(state, args[2].of.i32, 3);
    float *u = (float *)wasm_math_get(state, args[3].of.i32, 3);
    if (o && e && t && u)
        math_mat4_lookat(o, e, t, u);
    return NULL;
}

static wasm_trap_t *host_mat4_translate(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 16);
    float *v = (float *)wasm_math_get(state, args[2].of.i32, 3);
    if (o && a && v)
        math_mat4_translate(o, a, v);
    return NULL;
}

static wasm_trap_t *host_mat4_scale(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 16);
    float *v = (float *)wasm_math_get(state, args[2].of.i32, 3);
    if (o && a && v)
        math_mat4_scale(o, a, v);
    return NULL;
}

#define DEFINE_WASM_ROTATE(name, fn)                                  \
    static wasm_trap_t *host_##name(                                  \
        void *env, wasmtime_caller_t *caller,                         \
        const wasmtime_val_t *args, size_t nargs,                     \
        wasmtime_val_t *results, size_t nresults)                     \
    {                                                                 \
        HOST_PROLOGUE;                                                \
        float *o = (float *)wasm_math_get(state, args[0].of.i32, 16); \
        float *a = (float *)wasm_math_get(state, args[1].of.i32, 16); \
        if (o && a)                                                   \
            fn(o, a, args[2].of.f32);                                 \
        return NULL;                                                  \
    }

DEFINE_WASM_ROTATE(mat4_rotate_x, math_mat4_rotate_x)
DEFINE_WASM_ROTATE(mat4_rotate_y, math_mat4_rotate_y)
DEFINE_WASM_ROTATE(mat4_rotate_z, math_mat4_rotate_z)

static wasm_trap_t *host_mat4_invert(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    ApiError error;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 16);
    bool ok = math_service_mat4_invert(o, o ? 16 : 0,
                                       a, a ? 16 : 0, &error);
    if (!ok && error.status == API_STATUS_INVALID_ARGUMENT)
        wasm_math_report_error(&error);
    wasm_math_store_error(state, &error);
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ok ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_mat4_transpose(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 16);
    if (o && a)
        math_mat4_transpose(o, a);
    return NULL;
}

#define DEFINE_WASM_VEC3_BIN(name, fn)                               \
    static wasm_trap_t *host_##name(                                 \
        void *env, wasmtime_caller_t *caller,                        \
        const wasmtime_val_t *args, size_t nargs,                    \
        wasmtime_val_t *results, size_t nresults)                    \
    {                                                                \
        HOST_PROLOGUE;                                               \
        float *o = (float *)wasm_math_get(state, args[0].of.i32, 3); \
        float *a = (float *)wasm_math_get(state, args[1].of.i32, 3); \
        float *b = (float *)wasm_math_get(state, args[2].of.i32, 3); \
        if (o && a && b)                                             \
            fn(o, a, b);                                             \
        return NULL;                                                 \
    }

DEFINE_WASM_VEC3_BIN(vec3_add, math_vec3_add)
DEFINE_WASM_VEC3_BIN(vec3_sub, math_vec3_sub)
DEFINE_WASM_VEC3_BIN(vec3_cross, math_vec3_cross)

static wasm_trap_t *host_vec3_scale(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    ApiError error;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 3);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 3);
    if (!math_service_vec3_scale(o, o ? 3 : 0, a, a ? 3 : 0,
                                 args[2].of.f32, &error))
        wasm_math_report_error(&error);
    wasm_math_store_error(state, &error);
    return NULL;
}

static wasm_trap_t *host_vec3_normalize(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 3);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 3);
    if (o && a)
        math_vec3_normalize(o, a);
    return NULL;
}

static wasm_trap_t *host_vec3_dot(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *a = (float *)wasm_math_get(state, args[0].of.i32, 3);
    float *b = (float *)wasm_math_get(state, args[1].of.i32, 3);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = (a && b) ? math_vec3_dot(a, b) : 0.0f;
    return NULL;
}

static wasm_trap_t *host_vec3_length(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *a = (float *)wasm_math_get(state, args[0].of.i32, 3);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = a ? math_vec3_length(a) : 0.0f;
    return NULL;
}

static wasm_trap_t *host_vec3_transform_mat4(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 3);
    float *v = (float *)wasm_math_get(state, args[1].of.i32, 3);
    float *m = (float *)wasm_math_get(state, args[2].of.i32, 16);
    if (o && v && m)
        math_vec3_transform_mat4(o, v, m);
    return NULL;
}

static wasm_trap_t *host_quat_from_axis_angle(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 4);
    float *axis = (float *)wasm_math_get(state, args[1].of.i32, 3);
    if (o && axis)
        math_quat_from_axis_angle(o, axis, args[2].of.f32);
    return NULL;
}

static wasm_trap_t *host_quat_multiply(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 4);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 4);
    float *b = (float *)wasm_math_get(state, args[2].of.i32, 4);
    if (o && a && b)
        math_quat_multiply(o, a, b);
    return NULL;
}

static wasm_trap_t *host_quat_slerp(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 4);
    float *a = (float *)wasm_math_get(state, args[1].of.i32, 4);
    float *b = (float *)wasm_math_get(state, args[2].of.i32, 4);
    if (o && a && b)
        math_quat_slerp(o, a, b, args[3].of.f32);
    return NULL;
}

static wasm_trap_t *host_quat_to_mat4(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    HOST_PROLOGUE;
    float *o = (float *)wasm_math_get(state, args[0].of.i32, 16);
    float *q = (float *)wasm_math_get(state, args[1].of.i32, 4);
    if (o && q)
        math_quat_to_mat4(o, q);
    return NULL;
}

static wasmtime_error_t *define_math_func(
    wasmtime_linker_t *linker,
    WasmMathBindingState *state,
    const char *name,
    wasmtime_func_callback_t cb,
    const wasm_valkind_t *param_kinds, size_t nparams,
    const wasm_valkind_t *result_kinds, size_t nresults)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, nparams);
    for (size_t index = 0; index < nparams; index++)
        params.data[index] = wasm_valtype_new(param_kinds[index]);
    wasm_valtype_vec_new_uninitialized(&results, nresults);
    for (size_t index = 0; index < nresults; index++)
        results.data[index] = wasm_valtype_new(result_kinds[index]);

    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name),
        functype, cb, state, NULL);
    wasm_functype_delete(functype);
    return error;
}

#define DEF(name, cb, params, nparams, results, nresults)                \
    do                                                                   \
    {                                                                    \
        err = define_math_func(linker, state, name, cb, params, nparams, \
                               results, nresults);                       \
        if (err)                                                         \
            return err;                                                  \
    } while (0)

WasmMathBindingState *wasm_math_binding_state_create(void)
{
    return (WasmMathBindingState *)calloc(1, sizeof(WasmMathBindingState));
}

const ApiError *wasm_math_binding_state_error(
    const WasmMathBindingState *state)
{
    return state ? &state->error : NULL;
}

void wasm_math_binding_state_destroy(WasmMathBindingState *state)
{
    free(state);
}

wasmtime_error_t *wasm_math_register(wasmtime_linker_t *linker,
                                     WasmMathBindingState *state)
{
    wasmtime_error_t *err = NULL;
    static const wasm_valkind_t i = WASM_I32;
    static const wasm_valkind_t f = WASM_F32;
    
    const wasm_valkind_t p1i[1] = {i};
    const wasm_valkind_t p2i[2] = {i, i};
    const wasm_valkind_t p3i[3] = {i, i, i};
    const wasm_valkind_t p4i[4] = {i, i, i, i};
    const wasm_valkind_t p_2i_f[3] = {i, i, f};
    const wasm_valkind_t p_3i_f[4] = {i, i, i, f};
    const wasm_valkind_t p_i_5f[5] = {i, f, f, f, f};
    const wasm_valkind_t p_i_6f[7] = {i, f, f, f, f, f, f};
    const wasm_valkind_t r_i32[1] = {i};
    const wasm_valkind_t r_f32[1] = {f};

    if (!linker || !state)
        return wasmtime_error_new("WASM math binding state is required");

    DEF("math_mat4_identity", host_mat4_identity, p1i, 1, NULL, 0);
    DEF("math_mat4_multiply", host_mat4_multiply, p3i, 3, NULL, 0);
    DEF("math_mat4_perspective", host_mat4_perspective, p_i_5f, 5, NULL, 0);
    DEF("math_mat4_ortho", host_mat4_ortho, p_i_6f, 7, NULL, 0);
    DEF("math_mat4_lookat", host_mat4_lookat, p4i, 4, NULL, 0);
    DEF("math_mat4_translate", host_mat4_translate, p3i, 3, NULL, 0);
    DEF("math_mat4_rotate_x", host_mat4_rotate_x, p_2i_f, 3, NULL, 0);
    DEF("math_mat4_rotate_y", host_mat4_rotate_y, p_2i_f, 3, NULL, 0);
    DEF("math_mat4_rotate_z", host_mat4_rotate_z, p_2i_f, 3, NULL, 0);
    DEF("math_mat4_scale", host_mat4_scale, p3i, 3, NULL, 0);
    DEF("math_mat4_invert", host_mat4_invert, p2i, 2, r_i32, 1);
    DEF("math_mat4_transpose", host_mat4_transpose, p2i, 2, NULL, 0);

    DEF("math_vec3_add", host_vec3_add, p3i, 3, NULL, 0);
    DEF("math_vec3_sub", host_vec3_sub, p3i, 3, NULL, 0);
    DEF("math_vec3_scale", host_vec3_scale, p_2i_f, 3, NULL, 0);
    DEF("math_vec3_normalize", host_vec3_normalize, p2i, 2, NULL, 0);
    DEF("math_vec3_cross", host_vec3_cross, p3i, 3, NULL, 0);
    DEF("math_vec3_dot", host_vec3_dot, p2i, 2, r_f32, 1);
    DEF("math_vec3_length", host_vec3_length, p1i, 1, r_f32, 1);
    DEF("math_vec3_transform_mat4", host_vec3_transform_mat4, p3i, 3, NULL, 0);

    DEF("math_quat_from_axis_angle", host_quat_from_axis_angle, p_2i_f, 3, NULL, 0);
    DEF("math_quat_multiply", host_quat_multiply, p3i, 3, NULL, 0);
    DEF("math_quat_slerp", host_quat_slerp, p_3i_f, 4, NULL, 0);
    DEF("math_quat_to_mat4", host_quat_to_mat4, p2i, 2, NULL, 0);

    return NULL;
}

void wasm_math_set_memory(WasmMathBindingState *state,
                          wasmtime_context_t *store_ctx,
                          wasmtime_memory_t *memory)
{
    if (!state || !store_ctx || !memory)
        return;

    state->store_ctx = store_ctx;
    state->memory = *memory;
    state->has_memory = true;
}