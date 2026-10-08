#ifndef BUDO_WASM_ACCESSIBILITY_BINDINGS_H
#define BUDO_WASM_ACCESSIBILITY_BINDINGS_H

#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    wasmtime_error_t *wasm_accessibility_register(wasmtime_linker_t *linker);

#ifdef __cplusplus
}
#endif

#endif