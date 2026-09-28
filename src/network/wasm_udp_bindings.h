#ifndef WASM_UDP_BINDINGS_H
#define WASM_UDP_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "udp_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmUdpBindingState WasmUdpBindingState;

    WasmUdpBindingState *wasm_udp_binding_state_create(void);
    void wasm_udp_binding_state_destroy(WasmUdpBindingState *state);

    UdpContext *wasm_udp_context(WasmUdpBindingState *state);

    wasmtime_error_t *wasm_udp_register(wasmtime_linker_t *linker,
                                        WasmUdpBindingState *state);

    void wasm_udp_set_memory(WasmUdpBindingState *state,
                             wasmtime_context_t *store_ctx,
                             wasmtime_memory_t *memory);

    void wasm_udp_clear_memory(WasmUdpBindingState *state);

#ifdef __cplusplus
}
#endif

#endif