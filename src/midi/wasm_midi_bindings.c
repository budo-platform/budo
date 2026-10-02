#include "wasm_util/wasm_memory.h"
#include "wasm_midi_bindings.h"
#include "midi_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WasmMidiBindingState
{
    MidiContext *midi_ctx;
    RtpMidiContext *rtpmidi_ctx;
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool has_memory;
    bool midi_lazy_initialized;
};

static MidiContext *wasm_midi_context(WasmMidiBindingState *state)
{
    if (!state)
        return NULL;
    if (!state->midi_lazy_initialized)
    {
        state->midi_lazy_initialized = true;
        state->midi_ctx = midi_create();
        if (!state->midi_ctx)
            fprintf(stderr, "Warning: Failed to create WASM MIDI context\n");
    }
    return state->midi_ctx;
}

static bool wasm_midi_memory_range(int32_t pointer, int32_t length,
                                   size_t memory_size)
{
    return pointer >= 0 && length > 0 &&
           (size_t)pointer <= memory_size &&
           (size_t)length <= memory_size - (size_t)pointer;
}

#define WASM_MIDI_ENV ((WasmMidiBindingState *)env)
#define ensure_wasm_midi_ctx() ((void)wasm_midi_context(WASM_MIDI_ENV))

static inline wasmtime_memory_t *wasm_midi_memory(WasmMidiBindingState *state)
{
    return state->has_memory ? &state->memory : NULL;
}

static wasmtime_error_t *define_midi_func(
    wasmtime_linker_t *linker,
    WasmMidiBindingState *state,
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

static wasm_trap_t *host_midi_get_input_count(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = WASM_MIDI_ENV->midi_ctx ? midi_get_input_count(WASM_MIDI_ENV->midi_ctx) : 0;
    }
    return NULL;
}

static wasm_trap_t *host_midi_get_output_count(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = WASM_MIDI_ENV->midi_ctx ? midi_get_output_count(WASM_MIDI_ENV->midi_ctx) : 0;
    }
    return NULL;
}

static wasm_trap_t *host_midi_open_output(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 1)
        {
            ApiError error;
            results[0].of.i32 = midi_service_open_output(
                WASM_MIDI_ENV->midi_ctx, args[0].of.i32, &error);
        }
        else
        {
            results[0].of.i32 = -1;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_close_output(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)results;
    (void)nresults;
    ensure_wasm_midi_ctx();
    if (WASM_MIDI_ENV->midi_ctx && nargs >= 1)
    {
        midi_close_output(WASM_MIDI_ENV->midi_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_midi_send_message(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 4)
        {
            bool success = midi_send_message(WASM_MIDI_ENV->midi_ctx,
                                             args[0].of.i32,
                                             (uint8_t)args[1].of.i32,
                                             (uint8_t)args[2].of.i32,
                                             (uint8_t)args[3].of.i32);
            results[0].of.i32 = success ? 1 : 0;
        }
        else
        {
            results[0].of.i32 = 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_note_on(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 4)
        {
            bool success = midi_note_on(WASM_MIDI_ENV->midi_ctx,
                                        args[0].of.i32,
                                        args[1].of.i32,
                                        args[2].of.i32,
                                        args[3].of.i32);
            results[0].of.i32 = success ? 1 : 0;
        }
        else
        {
            results[0].of.i32 = 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_note_off(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 4)
        {
            bool success = midi_note_off(WASM_MIDI_ENV->midi_ctx,
                                         args[0].of.i32,
                                         args[1].of.i32,
                                         args[2].of.i32,
                                         args[3].of.i32);
            results[0].of.i32 = success ? 1 : 0;
        }
        else
        {
            results[0].of.i32 = 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_control_change(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 4)
        {
            bool success = midi_control_change(WASM_MIDI_ENV->midi_ctx,
                                               args[0].of.i32,
                                               args[1].of.i32,
                                               args[2].of.i32,
                                               args[3].of.i32);
            results[0].of.i32 = success ? 1 : 0;
        }
        else
        {
            results[0].of.i32 = 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_program_change(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 3)
        {
            bool success = midi_program_change(WASM_MIDI_ENV->midi_ctx,
                                               args[0].of.i32,
                                               args[1].of.i32,
                                               args[2].of.i32);
            results[0].of.i32 = success ? 1 : 0;
        }
        else
        {
            results[0].of.i32 = 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_pitch_bend(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (WASM_MIDI_ENV->midi_ctx && nargs >= 3)
        {
            bool success = midi_pitch_bend(WASM_MIDI_ENV->midi_ctx,
                                           args[0].of.i32,
                                           args[1].of.i32,
                                           args[2].of.i32);
            results[0].of.i32 = success ? 1 : 0;
        }
        else
        {
            results[0].of.i32 = 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_midi_send_raw(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    ensure_wasm_midi_ctx();
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = 0;

        if (WASM_MIDI_ENV->midi_ctx && WASM_MIDI_ENV->store_ctx && wasm_midi_memory(WASM_MIDI_ENV) && nargs >= 3)
        {
            int32_t handle = args[0].of.i32;
            int32_t data_ptr = args[1].of.i32;
            int32_t data_len = args[2].of.i32;

            uint8_t *mem = wasmtime_memory_data(WASM_MIDI_ENV->store_ctx, wasm_midi_memory(WASM_MIDI_ENV));
            size_t mem_size = wasmtime_memory_data_size(WASM_MIDI_ENV->store_ctx, wasm_midi_memory(WASM_MIDI_ENV));

            if (wasm_midi_memory_range(data_ptr, data_len, mem_size))
            {
                bool success = midi_send_raw(WASM_MIDI_ENV->midi_ctx, handle, mem + data_ptr, (size_t)data_len);
                results[0].of.i32 = success ? 1 : 0;
            }
        }
    }
    return NULL;
}

static wasm_trap_t *host_rtpmidi_create_session(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = -1;

        if (WASM_MIDI_ENV->rtpmidi_ctx && WASM_MIDI_ENV->store_ctx && wasm_midi_memory(WASM_MIDI_ENV) && nargs >= 3)
        {
            int32_t name_ptr = args[0].of.i32;
            int32_t name_len = args[1].of.i32;
            int32_t port = args[2].of.i32;

            uint8_t *mem = wasmtime_memory_data(WASM_MIDI_ENV->store_ctx, wasm_midi_memory(WASM_MIDI_ENV));
            size_t mem_size = wasmtime_memory_data_size(WASM_MIDI_ENV->store_ctx, wasm_midi_memory(WASM_MIDI_ENV));

            if (wasm_midi_memory_range(name_ptr, name_len, mem_size) &&
                name_len < RTPMIDI_MAX_NAME)
            {
                char name[RTPMIDI_MAX_NAME];
                memcpy(name, mem + name_ptr, name_len);
                name[name_len] = '\0';
                results[0].of.i32 = rtpmidi_create_session(WASM_MIDI_ENV->rtpmidi_ctx, name, port);
            }
        }
    }
    return NULL;
}

static wasm_trap_t *host_rtpmidi_connect(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = 0;

        if (WASM_MIDI_ENV->rtpmidi_ctx && WASM_MIDI_ENV->store_ctx && wasm_midi_memory(WASM_MIDI_ENV) && nargs >= 4)
        {
            int32_t session = args[0].of.i32;
            int32_t host_ptr = args[1].of.i32;
            int32_t host_len = args[2].of.i32;
            int32_t port = args[3].of.i32;

            uint8_t *mem = wasmtime_memory_data(WASM_MIDI_ENV->store_ctx, wasm_midi_memory(WASM_MIDI_ENV));
            size_t mem_size = wasmtime_memory_data_size(WASM_MIDI_ENV->store_ctx, wasm_midi_memory(WASM_MIDI_ENV));

            if (wasm_midi_memory_range(host_ptr, host_len, mem_size) &&
                host_len < 256)
            {
                char host[256];
                memcpy(host, mem + host_ptr, host_len);
                host[host_len] = '\0';
                results[0].of.i32 = rtpmidi_connect(WASM_MIDI_ENV->rtpmidi_ctx, session, host, port) ? 1 : 0;
            }
        }
    }
    return NULL;
}

static wasm_trap_t *host_rtpmidi_destroy_session(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)results;
    (void)nresults;
    if (WASM_MIDI_ENV->rtpmidi_ctx && nargs >= 1)
    {
        rtpmidi_destroy_session(WASM_MIDI_ENV->rtpmidi_ctx, args[0].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_rtpmidi_send_message(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = 0;
        if (WASM_MIDI_ENV->rtpmidi_ctx && nargs >= 4)
        {
            bool ok = rtpmidi_send_message(WASM_MIDI_ENV->rtpmidi_ctx,
                                           args[0].of.i32,
                                           (uint8_t)args[1].of.i32,
                                           (uint8_t)args[2].of.i32,
                                           (uint8_t)args[3].of.i32);
            results[0].of.i32 = ok ? 1 : 0;
        }
    }
    return NULL;
}

static wasm_trap_t *host_rtpmidi_get_session_count(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = WASM_MIDI_ENV->rtpmidi_ctx ? rtpmidi_get_session_count(WASM_MIDI_ENV->rtpmidi_ctx) : 0;
    }
    return NULL;
}

wasmtime_error_t *wasm_midi_register(wasmtime_linker_t *linker,
                                     WasmMidiBindingState *state)
{
    wasmtime_error_t *error = NULL;

    if (!linker || !state)
        return wasmtime_error_new("WASM MIDI binding state is required");

    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_get_input_count", host_midi_get_input_count, NULL, 0, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_get_output_count", host_midi_get_output_count, NULL, 0, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_open_output", host_midi_open_output, params, 1, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_close_output", host_midi_close_output, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_send_message", host_midi_send_message, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_note_on", host_midi_note_on, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_note_off", host_midi_note_off, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_control_change", host_midi_control_change, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_program_change", host_midi_program_change, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_pitch_bend", host_midi_pitch_bend, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "midi_send_raw", host_midi_send_raw, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "rtpmidi_create_session", host_rtpmidi_create_session, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "rtpmidi_connect", host_rtpmidi_connect, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_midi_func(linker, state, "rtpmidi_destroy_session", host_rtpmidi_destroy_session, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "rtpmidi_send_message", host_rtpmidi_send_message, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_midi_func(linker, state, "rtpmidi_get_session_count", host_rtpmidi_get_session_count, NULL, 0, results, 1);
        if (error)
            return error;
    }

    return NULL;
}

WasmMidiBindingState *wasm_midi_binding_state_create(void)
{
    return calloc(1, sizeof(WasmMidiBindingState));
}

void wasm_midi_binding_state_destroy(WasmMidiBindingState *state)
{
    if (!state)
        return;
    midi_destroy(state->midi_ctx);
    state->midi_ctx = NULL;
    state->rtpmidi_ctx = NULL;
    state->store_ctx = NULL;
    state->has_memory = false;
    free(state);
}

void wasm_midi_set_context(WasmMidiBindingState *state, MidiContext *ctx)
{
    if (!state)
        return;
    state->midi_ctx = ctx;
    state->midi_lazy_initialized = ctx != NULL;
}

void wasm_midi_set_memory(WasmMidiBindingState *state,
                          wasmtime_context_t *store_ctx,
                          wasmtime_memory_t *memory)
{
    if (!state)
        return;
    state->store_ctx = store_ctx;
    state->has_memory = memory != NULL;
    if (memory)
        state->memory = *memory;
}

void wasm_midi_clear_memory(WasmMidiBindingState *state)
{
    if (!state)
        return;
    state->store_ctx = NULL;
    state->has_memory = false;
}

void wasm_midi_set_rtpmidi(WasmMidiBindingState *state,
                           RtpMidiContext *rtp_ctx)
{
    if (state)
        state->rtpmidi_ctx = rtp_ctx;
}

void wasm_midi_poll(WasmMidiBindingState *state)
{
    if (state && state->rtpmidi_ctx)
        rtpmidi_poll(state->rtpmidi_ctx);
}

#undef ensure_wasm_midi_ctx
#undef WASM_MIDI_ENV