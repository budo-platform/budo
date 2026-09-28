#ifndef WASM_NETWORK_BINDINGS_H
#define WASM_NETWORK_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "network_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmNetworkBindingState WasmNetworkBindingState;

    WasmNetworkBindingState *wasm_network_binding_state_create(
        const char *project_dir);

    void wasm_network_binding_state_destroy(WasmNetworkBindingState *state);

    void wasm_network_binding_scope_enter(WasmNetworkBindingState *state);
    void wasm_network_binding_scope_leave(WasmNetworkBindingState *state);

    wasmtime_error_t *wasm_network_register(wasmtime_linker_t *linker);

    wasmtime_error_t *wasm_network_register_state(
        wasmtime_linker_t *linker, WasmNetworkBindingState *state);

    NetworkContext *wasm_network_context(WasmNetworkBindingState *state);

    void wasm_network_set_memory(wasmtime_context_t *store_ctx,
                                 wasmtime_memory_t *memory);

    void wasm_network_binding_state_set_memory(
        WasmNetworkBindingState *state, wasmtime_context_t *store_ctx,
        wasmtime_memory_t *memory);

    void wasm_network_poll(WasmNetworkBindingState *state);

    void wasm_network_shutdown(WasmNetworkBindingState *state);

    void wasm_network_cleanup(WasmNetworkBindingState *state);

#ifdef __cplusplus
}
#endif

#endif