#ifndef WASM_MATH_BINDINGS_H
#define WASM_MATH_BINDINGS_H

#include "core/api_error.h"

#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmMathBindingState WasmMathBindingState;

    WasmMathBindingState *wasm_math_binding_state_create(void);
    void wasm_math_binding_state_destroy(WasmMathBindingState *state);
    const ApiError *wasm_math_binding_state_error(
        const WasmMathBindingState *state);
    wasmtime_error_t *wasm_math_register(wasmtime_linker_t *linker,
                                         WasmMathBindingState *state);
    void wasm_math_set_memory(WasmMathBindingState *state,
                              wasmtime_context_t *store_ctx,
                              wasmtime_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif