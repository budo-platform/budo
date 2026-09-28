#ifndef WASM_NEURAL_BINDINGS_H
#define WASM_NEURAL_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "neural_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmNeuralBindingState WasmNeuralBindingState;

    WasmNeuralBindingState *wasm_neural_binding_state_create(void);
    void wasm_neural_binding_state_destroy(WasmNeuralBindingState *state);

    bool wasm_neural_binding_state_enable(WasmNeuralBindingState *state,
                                          const char *project_dir);

    wasmtime_error_t *wasm_neural_register(wasmtime_linker_t *linker,
                                           WasmNeuralBindingState *state);

    void wasm_neural_binding_state_set_memory(WasmNeuralBindingState *state,
                                              wasmtime_context_t *store_ctx,
                                              wasmtime_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif