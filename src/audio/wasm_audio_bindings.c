#include "wasm_util/wasm_memory.h"
#include "wasm_audio_bindings.h"
#include "audio_service.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WasmAudioBindingState
{
    AudioContext *audio_ctx;
    bool lazy_initialized;
    char asset_root[4096];
};

static AudioContext *wasm_audio_context(WasmAudioBindingState *state)
{
    if (!state)
        return NULL;
    if (!state->lazy_initialized)
    {
        state->lazy_initialized = true;
        state->audio_ctx = audio_create();
        if (state->audio_ctx)
            audio_set_asset_root(state->audio_ctx, state->asset_root);
        else
            fprintf(stderr, "Warning: Failed to create WASM audio context\n");
    }
    return state->audio_ctx;
}

#define WASM_AUDIO_CONTEXT()                                     \
    WasmAudioBindingState *state = (WasmAudioBindingState *)env; \
    AudioContext *g_wasm_audio_ctx = wasm_audio_context(state)

static wasmtime_error_t *define_audio_func(
    wasmtime_linker_t *linker,
    WasmAudioBindingState *state,
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

static wasm_trap_t *host_audio_start(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)args;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (g_wasm_audio_ctx)
    {
        audio_start(g_wasm_audio_ctx);
    }
    return NULL;
}

static wasm_trap_t *host_audio_stop(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)args;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (g_wasm_audio_ctx)
    {
        audio_stop(g_wasm_audio_ctx);
    }
    return NULL;
}

static wasm_trap_t *host_audio_set_master_gain(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 1)
    {
        audio_set_master_gain(g_wasm_audio_ctx, args[0].of.f32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_create_oscillator(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)args;
    (void)nargs;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        ApiError error;
        results[0].of.i32 = audio_service_create_oscillator(g_wasm_audio_ctx, &error);
    }
    return NULL;
}

static wasm_trap_t *host_audio_destroy_oscillator(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 1)
    {
        audio_destroy_oscillator(g_wasm_audio_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_set_type(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 2)
    {
        audio_oscillator_set_type(g_wasm_audio_ctx, args[0].of.i32, (AudioWaveType)args[1].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_set_frequency(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 2)
    {
        audio_oscillator_set_frequency(g_wasm_audio_ctx, args[0].of.i32, args[1].of.f32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_set_gain(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 2)
    {
        audio_oscillator_set_gain(g_wasm_audio_ctx, args[0].of.i32, args[1].of.f32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_set_detune(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 2)
    {
        audio_oscillator_set_detune(g_wasm_audio_ctx, args[0].of.i32, args[1].of.f32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_start(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 1)
    {
        audio_oscillator_start(g_wasm_audio_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_stop(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 1)
    {
        audio_oscillator_stop(g_wasm_audio_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_set_envelope(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 5)
    {
        audio_oscillator_set_envelope(g_wasm_audio_ctx, args[0].of.i32,
                                      args[1].of.f32, args[2].of.f32,
                                      args[3].of.f32, args[4].of.f32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_note_on(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 1)
    {
        audio_oscillator_note_on(g_wasm_audio_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_oscillator_note_off(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WASM_AUDIO_CONTEXT();
    (void)caller;
    (void)nresults;
    (void)results;
    if (g_wasm_audio_ctx && nargs >= 1)
    {
        audio_oscillator_note_off(g_wasm_audio_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_audio_midi_to_freq(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    if (nresults >= 1 && nargs >= 1)
    {
        results[0].kind = WASMTIME_F32;
        results[0].of.f32 = audio_midi_to_freq(args[0].of.i32);
    }
    return NULL;
}

wasmtime_error_t *wasm_audio_register(wasmtime_linker_t *linker,
                                      WasmAudioBindingState *state)
{
    wasmtime_error_t *error = NULL;

    if (!linker || !state)
        return wasmtime_error_new("WASM audio binding state is required");

    error = define_audio_func(linker, state, "audio_start", host_audio_start, NULL, 0, NULL, 0);
    if (error)
        return error;

    error = define_audio_func(linker, state, "audio_stop", host_audio_stop, NULL, 0, NULL, 0);
    if (error)
        return error;

    {
        wasm_valkind_t params[] = {WASM_F32};
        error = define_audio_func(linker, state, "audio_set_master_gain", host_audio_set_master_gain, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_audio_func(linker, state, "audio_create_oscillator", host_audio_create_oscillator, NULL, 0, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_audio_func(linker, state, "audio_destroy_oscillator", host_audio_destroy_oscillator, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        error = define_audio_func(linker, state, "audio_oscillator_set_type", host_audio_oscillator_set_type, params, 2, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32};
        error = define_audio_func(linker, state, "audio_oscillator_set_frequency", host_audio_oscillator_set_frequency, params, 2, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32};
        error = define_audio_func(linker, state, "audio_oscillator_set_gain", host_audio_oscillator_set_gain, params, 2, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32};
        error = define_audio_func(linker, state, "audio_oscillator_set_detune", host_audio_oscillator_set_detune, params, 2, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_audio_func(linker, state, "audio_oscillator_start", host_audio_oscillator_start, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_audio_func(linker, state, "audio_oscillator_stop", host_audio_oscillator_stop, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_audio_func(linker, state, "audio_oscillator_set_envelope", host_audio_oscillator_set_envelope, params, 5, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_audio_func(linker, state, "audio_oscillator_note_on", host_audio_oscillator_note_on, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_audio_func(linker, state, "audio_oscillator_note_off", host_audio_oscillator_note_off, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_audio_func(linker, state, "audio_midi_to_freq", host_audio_midi_to_freq, params, 1, results, 1);
        if (error)
            return error;
    }

    return NULL;
}

WasmAudioBindingState *wasm_audio_binding_state_create(const char *asset_root)
{
    WasmAudioBindingState *state =
        (WasmAudioBindingState *)calloc(1, sizeof(WasmAudioBindingState));
    if (!state)
        return NULL;
    snprintf(state->asset_root, sizeof(state->asset_root), "%s",
             asset_root ? asset_root : ".");
    return state;
}

void wasm_audio_binding_state_destroy(WasmAudioBindingState *state)
{
    if (!state)
        return;
    audio_destroy(state->audio_ctx);
    free(state);
}

#undef WASM_AUDIO_CONTEXT