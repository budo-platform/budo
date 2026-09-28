#ifndef WASM_FILE_BINDINGS_H
#define WASM_FILE_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "file_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmFileBindingState WasmFileBindingState;

    WasmFileBindingState *wasm_file_binding_state_create(const char *root_dir);
    void wasm_file_binding_state_destroy(WasmFileBindingState *state);
    FileContext *wasm_file_context(WasmFileBindingState *state);

    wasmtime_error_t *wasm_file_register_state(wasmtime_linker_t *linker,
                                               WasmFileBindingState *state);

    void wasm_file_binding_state_set_memory(WasmFileBindingState *state,
                                            wasmtime_context_t *store_ctx,
                                            wasmtime_memory_t *memory);

    void wasm_file_binding_scope_enter(WasmFileBindingState *state);
    void wasm_file_binding_scope_leave(WasmFileBindingState *state);
    wasmtime_error_t *wasm_file_register(wasmtime_linker_t *linker);
    void wasm_file_scoped_state_set_memory(wasmtime_context_t *store_ctx,
                                           wasmtime_memory_t *memory);

#define wasm_file_set_memory(store_ctx, memory) \
    wasm_file_scoped_state_set_memory((store_ctx), (memory))

#ifdef __cplusplus
}
#endif

#endif