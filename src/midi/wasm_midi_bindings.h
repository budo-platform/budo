#ifndef WASM_MIDI_BINDINGS_H
#define WASM_MIDI_BINDINGS_H

#include <stdbool.h>
#include <wasmtime.h>
#include "midi_wrapper.h"
#include "rtpmidi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmMidiBindingState WasmMidiBindingState;

    WasmMidiBindingState *wasm_midi_binding_state_create(void);
    void wasm_midi_binding_state_destroy(WasmMidiBindingState *state);

    wasmtime_error_t *wasm_midi_register(wasmtime_linker_t *linker,
                                         WasmMidiBindingState *state);

    void wasm_midi_set_context(WasmMidiBindingState *state, MidiContext *ctx);

    void wasm_midi_set_memory(WasmMidiBindingState *state,
                              wasmtime_context_t *store_ctx,
                              wasmtime_memory_t *memory);

    void wasm_midi_clear_memory(WasmMidiBindingState *state);

    void wasm_midi_set_rtpmidi(WasmMidiBindingState *state,
                               RtpMidiContext *rtp_ctx);

    void wasm_midi_poll(WasmMidiBindingState *state);

#ifdef __cplusplus
}
#endif

#endif