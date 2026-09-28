#include "wasm_udp_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WasmUdpBindingState
{
    UdpContext *udp_ctx;
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool has_memory;
};

static wasmtime_error_t *define_udp_func(
    wasmtime_linker_t *linker,
    WasmUdpBindingState *state,
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

static wasm_trap_t *host_udp_bind(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmUdpBindingState *state = (WasmUdpBindingState *)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (state && state->udp_ctx && nargs >= 1)
        {
            results[0].of.i32 = udp_bind(state->udp_ctx, args[0].of.i32);
        }
        else
        {
            results[0].of.i32 = -1;
        }
    }
    return NULL;
}

static wasm_trap_t *host_udp_get_port(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmUdpBindingState *state = (WasmUdpBindingState *)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        if (state && state->udp_ctx && nargs >= 1)
        {
            results[0].of.i32 = udp_get_port(state->udp_ctx, args[0].of.i32);
        }
        else
        {
            results[0].of.i32 = -1;
        }
    }
    return NULL;
}

static wasm_trap_t *host_udp_send(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmUdpBindingState *state = (WasmUdpBindingState *)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = 0;

        if (state && state->udp_ctx && state->store_ctx && state->has_memory && nargs >= 6)
        {
            int32_t handle = args[0].of.i32;
            int32_t host_ptr = args[1].of.i32;
            int32_t host_len = args[2].of.i32;
            int32_t port = args[3].of.i32;
            int32_t data_ptr = args[4].of.i32;
            int32_t data_len = args[5].of.i32;

            uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
            size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

            if (host_ptr >= 0 && host_len > 0 && (size_t)(host_ptr + host_len) <= mem_size &&
                data_ptr >= 0 && data_len > 0 && (size_t)(data_ptr + data_len) <= mem_size &&
                host_len < 256)
            {
                char host[256];
                memcpy(host, mem + host_ptr, host_len);
                host[host_len] = '\0';

                bool ok = udp_send(state->udp_ctx, handle, host, port,
                                   mem + data_ptr, data_len);
                results[0].of.i32 = ok ? 1 : 0;
            }
        }
    }
    return NULL;
}

static wasm_trap_t *host_udp_recv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmUdpBindingState *state = (WasmUdpBindingState *)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = 0;

        if (state && state->udp_ctx && state->store_ctx && state->has_memory && nargs >= 4)
        {
            int32_t handle = args[0].of.i32;
            int32_t buf_ptr = args[1].of.i32;
            int32_t buf_len = args[2].of.i32;
            int32_t info_ptr = args[3].of.i32;

            uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
            size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

            if (buf_ptr >= 0 && buf_len > 0 && (size_t)(buf_ptr + buf_len) <= mem_size &&
                info_ptr >= 0 && (size_t)(info_ptr + 68) <= mem_size)
            {
                UdpDatagram dgram;
                int received = udp_recv(state->udp_ctx, handle,
                                        mem + buf_ptr, buf_len, &dgram);
                if (received > 0)
                {
                    
                    uint32_t port_val = (uint32_t)dgram.port;
                    memcpy(mem + info_ptr, &port_val, 4);
                    size_t host_len = strlen(dgram.host);
                    if (host_len > 63)
                        host_len = 63;
                    memcpy(mem + info_ptr + 4, dgram.host, host_len);
                    mem[info_ptr + 4 + host_len] = '\0';
                }
                results[0].of.i32 = received;
            }
        }
    }
    return NULL;
}

static wasm_trap_t *host_udp_close(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmUdpBindingState *state = (WasmUdpBindingState *)env;
    (void)caller;
    (void)results;
    (void)nresults;
    if (state && state->udp_ctx && nargs >= 1)
    {
        udp_close(state->udp_ctx, args[0].of.i32);
    }
    return NULL;
}

wasmtime_error_t *wasm_udp_register(wasmtime_linker_t *linker,
                                    WasmUdpBindingState *state)
{
    wasmtime_error_t *error = NULL;

    if (!linker || !state)
        return wasmtime_error_new("WASM UDP binding state is required");

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_udp_func(linker, state, "udp_bind", host_udp_bind, params, 1, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_udp_func(linker, state, "udp_get_port", host_udp_get_port, params, 1, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_udp_func(linker, state, "udp_send", host_udp_send, params, 6, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_udp_func(linker, state, "udp_recv", host_udp_recv, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_udp_func(linker, state, "udp_close", host_udp_close, params, 1, NULL, 0);
        if (error)
            return error;
    }

    return NULL;
}

WasmUdpBindingState *wasm_udp_binding_state_create(void)
{
    WasmUdpBindingState *state =
        (WasmUdpBindingState *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;

    state->udp_ctx = udp_create();
    if (!state->udp_ctx)
    {
        fprintf(stderr, "Warning: Failed to create UDP context\n");
    }
    return state;
}

void wasm_udp_binding_state_destroy(WasmUdpBindingState *state)
{
    if (!state)
        return;

    wasm_udp_clear_memory(state);
    udp_destroy(state->udp_ctx);
    state->udp_ctx = NULL;
    free(state);
}

UdpContext *wasm_udp_context(WasmUdpBindingState *state)
{
    return state ? state->udp_ctx : NULL;
}

void wasm_udp_set_memory(WasmUdpBindingState *state,
                         wasmtime_context_t *store_ctx,
                         wasmtime_memory_t *memory)
{
    if (!state || !store_ctx || !memory)
        return;
    state->store_ctx = store_ctx;
    state->memory = *memory;
    state->has_memory = true;
}

void wasm_udp_clear_memory(WasmUdpBindingState *state)
{
    if (!state)
        return;
    state->has_memory = false;
    state->store_ctx = NULL;
    memset(&state->memory, 0, sizeof(state->memory));
}