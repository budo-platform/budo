#ifndef WASM_AUDIO_BINDINGS_H
#define WASM_AUDIO_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "audio_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmAudioBindingState WasmAudioBindingState;

    WasmAudioBindingState *wasm_audio_binding_state_create(const char *asset_root);
    void wasm_audio_binding_state_destroy(WasmAudioBindingState *state);

    wasmtime_error_t *wasm_audio_register(wasmtime_linker_t *linker,
                                          WasmAudioBindingState *state);

#ifdef __cplusplus
}
#endif

#endif