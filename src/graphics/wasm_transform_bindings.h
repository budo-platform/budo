#ifndef BUDO_WASM_TRANSFORM_BINDINGS_H
#define BUDO_WASM_TRANSFORM_BINDINGS_H

#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmCanvasContext WasmCanvasContext;

    wasmtime_error_t *wasm_transform_register(wasmtime_linker_t *linker,
                                              WasmCanvasContext *context);

#ifdef __cplusplus
}
#endif

#endif