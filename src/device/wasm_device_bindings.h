#ifndef WASM_DEVICE_BINDINGS_H
#define WASM_DEVICE_BINDINGS_H

#include "device_service.h"

#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    wasmtime_error_t *wasm_device_register(wasmtime_linker_t *linker,
                                           DeviceContext *state);

#ifdef __cplusplus
}
#endif

#endif