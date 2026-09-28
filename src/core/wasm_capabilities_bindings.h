#ifndef BUDO_WASM_CAPABILITIES_BINDINGS_H
#define BUDO_WASM_CAPABILITIES_BINDINGS_H

#include <wasm.h>
#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    wasmtime_error_t *wasm_capabilities_register(wasmtime_linker_t *linker);

#ifdef __cplusplus
}
#endif

#endif