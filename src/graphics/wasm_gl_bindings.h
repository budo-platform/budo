#ifndef BUDO_WASM_GL_BINDINGS_H
#define BUDO_WASM_GL_BINDINGS_H

#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmCanvasContext WasmCanvasContext;

    wasmtime_error_t *wasm_gl_register(wasmtime_linker_t *linker,
                                       WasmCanvasContext *context);

#ifdef __cplusplus
}
#endif

#endif