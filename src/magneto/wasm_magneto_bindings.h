#ifndef WASM_MAGNETO_BINDINGS_H
#define WASM_MAGNETO_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "magneto_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmMagnetoBindingState WasmMagnetoBindingState;

    WasmMagnetoBindingState *wasm_magneto_binding_state_create(void);
    void wasm_magneto_binding_state_destroy(WasmMagnetoBindingState *state);

    wasmtime_error_t *wasm_magneto_register(wasmtime_linker_t *linker,
                                            WasmMagnetoBindingState *state);

#ifdef __cplusplus
}
#endif

#endif