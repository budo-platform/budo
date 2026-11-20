#ifndef BUDO_WASM_GL_BINDINGS_H
#define BUDO_WASM_GL_BINDINGS_H

#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmCanvasContext WasmCanvasContext;
    typedef struct WasmImageBindingState WasmImageBindingState;
    typedef struct Window Window;

    void wasm_image_binding_state_destroy(WasmImageBindingState *state, Window *window);

    wasmtime_error_t *wasm_gl_register(wasmtime_linker_t *linker,
                                       WasmCanvasContext *context);

#ifdef __cplusplus
}
#endif

#endif