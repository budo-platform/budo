#ifndef WASM_SQLITE_BINDINGS_H
#define WASM_SQLITE_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "sqlite_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmSqliteBindingState WasmSqliteBindingState;

    WasmSqliteBindingState *wasm_sqlite_binding_state_create(const char *project_dir);
    void wasm_sqlite_binding_state_destroy(WasmSqliteBindingState *state);

    wasmtime_error_t *wasm_sqlite_register(wasmtime_linker_t *linker,
                                           WasmSqliteBindingState *state);

    void wasm_sqlite_set_memory(WasmSqliteBindingState *state,
                                wasmtime_context_t *store_ctx,
                                wasmtime_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif