#ifndef WASM_CANVAS_BINDINGS_H
#define WASM_CANVAS_BINDINGS_H

#include "core/animation_wait.h"
#include <stdbool.h>
#include "core/graphics_activation.h"
#include <stddef.h>
#include <stdint.h>
#include "graphics/skia_wrapper.h"
#include "core/input.h"
#include "file/file_wrapper.h"
#include "network/udp_wrapper.h"
#include "graphics/image_scheduler.h"

typedef struct Window Window;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct WasmCanvasContext WasmCanvasContext;
    typedef struct WasmImageBindingState WasmImageBindingState;

    WasmCanvasContext *wasm_canvas_create(const char *project_dir, const char *sqlite_dir);

    SkiaCanvas *wasm_canvas_current_canvas(WasmCanvasContext *ctx);
    
    WasmCanvasContext *wasm_canvas_graphics(void *env); 
    SkiaPaint *wasm_canvas_active_paint(WasmCanvasContext *ctx);
    
    BudoAnimationWait *wasm_canvas_animation_wait(WasmCanvasContext *ctx, int *width, int *height);
    SkiaPath *wasm_canvas_path(WasmCanvasContext *ctx, int id);
    
    uint8_t *wasm_canvas_guest_bytes(WasmCanvasContext *ctx, int32_t ptr, int32_t len);
    Window *wasm_canvas_current_window(WasmCanvasContext *ctx);
    
    bool wasm_canvas_exit_requested(const WasmCanvasContext *ctx, int *code);
    
    bool wasm_canvas_graphics_requested(const WasmCanvasContext *ctx);
    
    void wasm_canvas_set_graphics_activation(WasmCanvasContext *ctx,
                                             BudoGraphicsActivateFn activate, void *opaque);
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

    ImageScheduler *wasm_canvas_image_scheduler(WasmCanvasContext *ctx);
    WasmImageBindingState **wasm_canvas_image_state_slot(WasmCanvasContext *ctx);
    uint32_t wasm_canvas_image_load(WasmCanvasContext *ctx, const char *path,
                                    const uint8_t *bytes, size_t length,
                                    const ImageAsyncOptions *options);
    int wasm_canvas_image_status(WasmCanvasContext *ctx, uint32_t request, uint8_t *output);
    int wasm_canvas_image_take(WasmCanvasContext *ctx, uint32_t request);
    bool wasm_canvas_image_release(WasmCanvasContext *ctx, uint32_t request);
    void wasm_canvas_image_poll(WasmCanvasContext *ctx);
    bool wasm_canvas_image_pending(WasmCanvasContext *ctx);

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