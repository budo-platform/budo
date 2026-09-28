#ifndef WASM_CANVAS_BINDINGS_H
#define WASM_CANVAS_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "graphics/skia_wrapper.h"
#include "core/input.h"
#include "file/file_wrapper.h"
#include "network/udp_wrapper.h"

typedef struct Window Window;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmCanvasContext WasmCanvasContext;

    WasmCanvasContext *wasm_canvas_create(const char *project_dir);

    SkiaCanvas *wasm_canvas_current_canvas(WasmCanvasContext *ctx);
    Window *wasm_canvas_current_window(WasmCanvasContext *ctx);
    bool wasm_canvas_read_string(WasmCanvasContext *ctx, int32_t pointer,
                                 int32_t length, char *output,
                                 size_t output_size);
    const uint8_t *wasm_canvas_read_bytes(WasmCanvasContext *ctx,
                                          int32_t pointer, int32_t length);
    void wasm_canvas_set_error(WasmCanvasContext *ctx, const char *message);

#ifdef BUDO_NEURAL

    bool wasm_canvas_enable_neural(WasmCanvasContext *ctx);
#endif

    void wasm_canvas_destroy(WasmCanvasContext *ctx);

    UdpContext *wasm_canvas_udp_context(WasmCanvasContext *ctx);

    void wasm_canvas_midi_poll(WasmCanvasContext *ctx);

    void wasm_canvas_network_poll(WasmCanvasContext *ctx);

    FileContext *wasm_canvas_file_context(WasmCanvasContext *ctx);

    bool wasm_canvas_load_wat_file(WasmCanvasContext *ctx, const char *filename);

    bool wasm_canvas_load_wasm_file(WasmCanvasContext *ctx, const char *filename);

    void wasm_canvas_set_context(WasmCanvasContext *ctx, SkiaCanvas *canvas,
                                 InputState *input, Window *window, int width, int height,
                                 float display_density);

    bool wasm_canvas_call_animation(WasmCanvasContext *ctx, double timestamp);

    bool wasm_canvas_has_animation(WasmCanvasContext *ctx);

    const char *wasm_canvas_get_error(WasmCanvasContext *ctx);

#ifdef __cplusplus
}
#endif

#endif