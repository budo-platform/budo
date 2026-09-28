#include "graphics/wasm_canvas_bindings.h"
#include "graphics/wasm_gl_bindings.h"
#include "graphics/wasm_transform_bindings.h"
#include "audio/wasm_audio_bindings.h"
#include "midi/wasm_midi_bindings.h"
#include "sqlite/wasm_sqlite_bindings.h"
#include "file/wasm_file_bindings.h"
#include "magneto/wasm_magneto_bindings.h"
#include "device/wasm_device_bindings.h"
#ifdef BUDO_NEURAL
#include "neural/wasm_neural_bindings.h"
#endif
#include "math/wasm_math_bindings.h"
#include "network/wasm_network_bindings.h"
#include "network/wasm_udp_bindings.h"
#include "core/wasm_capabilities_bindings.h"
#include "core/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wasm.h>
#include <wasmtime.h>

#define MAX_PATHS 1024
#define MAX_WASM_STRING 512

struct WasmCanvasContext
{
    
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_instance_t instance;
    wasmtime_linker_t *linker;
    WasmAudioBindingState *audio_binding_state;
    DeviceContext *device_binding_state;
    WasmMathBindingState *math_binding_state;
    WasmSqliteBindingState *sqlite_binding_state;
    WasmFileBindingState *file_binding_state;
    WasmNetworkBindingState *network_binding_state;
    WasmMagnetoBindingState *magneto_binding_state;
#ifdef BUDO_NEURAL
    WasmNeuralBindingState *neural_binding_state;
#endif
    WasmUdpBindingState *udp_binding_state;
    WasmMidiBindingState *midi_binding_state;
    RtpMidiContext *rtpmidi_ctx;

    wasmtime_func_t init_func;
    wasmtime_func_t frame_func;
    bool has_init_func;
    bool has_frame_func;

    wasmtime_memory_t memory;
    bool has_memory;

    SkiaCanvas *canvas;
    SkiaPaint *active_paint;

    InputState *input;

    Window *window;

    int width;
    int height;

    float display_density;

    SkiaPath *paths[MAX_PATHS];
    int path_count;

    SkiaSVG *svgs[MAX_PATHS];
    int svg_count;

    char error_msg[512];
    char project_dir[1024];
};

static void set_error(WasmCanvasContext *ctx, const char *msg)
{
    if (ctx && msg)
    {
        strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
        ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
    }
}

static void set_wasmtime_error(WasmCanvasContext *ctx, wasmtime_error_t *error, wasm_trap_t *trap)
{
    if (error)
    {
        wasm_byte_vec_t msg;
        wasmtime_error_message(error, &msg);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Wasmtime error: %.*s",
                 (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
    }
    else if (trap)
    {
        wasm_byte_vec_t msg;
        wasm_trap_message(trap, &msg);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Trap: %.*s",
                 (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasm_trap_delete(trap);
    }
}

static int add_path(WasmCanvasContext *ctx, SkiaPath *path)
{
    if (ctx->path_count >= MAX_PATHS)
        return -1;
    ctx->paths[ctx->path_count] = path;
    return ctx->path_count++;
}

static SkiaPath *get_path(WasmCanvasContext *ctx, int id)
{
    if (id < 0 || id >= ctx->path_count)
        return NULL;
    return ctx->paths[id];
}

static bool read_wasm_string(WasmCanvasContext *ctx, int32_t ptr, int32_t len,
                             char *buffer, size_t buffer_size)
{
    uint8_t *memory_data;
    size_t memory_size;
    size_t end;

    if (!ctx || !ctx->has_memory)
    {
        set_error(ctx, "WebAssembly module does not export memory");
        return false;
    }

    if (ptr < 0 || len < 0 || !buffer || buffer_size == 0)
    {
        set_error(ctx, "Invalid WebAssembly string range");
        return false;
    }

    if ((size_t)len + 1 > buffer_size)
    {
        set_error(ctx, "WebAssembly string is too long");
        return false;
    }

    memory_data = wasmtime_memory_data(ctx->context, &ctx->memory);
    memory_size = wasmtime_memory_data_size(ctx->context, &ctx->memory);
    end = (size_t)ptr + (size_t)len;
    if (end < (size_t)ptr || end > memory_size)
    {
        set_error(ctx, "WebAssembly string is out of bounds");
        return false;
    }

    memcpy(buffer, memory_data + ptr, (size_t)len);
    buffer[len] = '\0';
    return true;
}

static const uint8_t *read_wasm_bytes(WasmCanvasContext *ctx, int32_t ptr, int32_t len)
{
    uint8_t *memory_data;
    size_t memory_size;
    size_t end;

    if (!ctx || !ctx->has_memory)
    {
        set_error(ctx, "WebAssembly module does not export memory");
        return NULL;
    }

    if (ptr < 0 || len <= 0)
    {
        set_error(ctx, "Invalid WebAssembly buffer range");
        return NULL;
    }

    memory_data = wasmtime_memory_data(ctx->context, &ctx->memory);
    memory_size = wasmtime_memory_data_size(ctx->context, &ctx->memory);
    end = (size_t)ptr + (size_t)len;
    if (end < (size_t)ptr || end > memory_size)
    {
        set_error(ctx, "WebAssembly buffer is out of bounds");
        return NULL;
    }

    return memory_data + ptr;
}

#define CANVAS_CALLBACK_CONTEXT ((WasmCanvasContext *)env)

static wasm_trap_t *host_canvas_clear(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        uint32_t color = args[0].of.i32;
        skia_canvas_clear(CANVAS_CALLBACK_CONTEXT->canvas, color);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_read_pixels(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    int32_t ptr = args[0].of.i32;
    int32_t max_len = args[1].of.i32;
    int32_t written = 0;

    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas && CANVAS_CALLBACK_CONTEXT->has_memory && ptr >= 0 && max_len > 0)
    {
        int w = 0, h = 0;
        if (skia_canvas_get_size(CANVAS_CALLBACK_CONTEXT->canvas, &w, &h) && w > 0 && h > 0)
        {
            size_t needed = (size_t)w * (size_t)h * 4u;
            uint8_t *mem = wasmtime_memory_data(CANVAS_CALLBACK_CONTEXT->context, &CANVAS_CALLBACK_CONTEXT->memory);
            size_t mem_size = wasmtime_memory_data_size(CANVAS_CALLBACK_CONTEXT->context, &CANVAS_CALLBACK_CONTEXT->memory);
            if ((size_t)max_len >= needed && (size_t)ptr + needed <= mem_size)
            {
                if (skia_canvas_read_pixels(CANVAS_CALLBACK_CONTEXT->canvas, mem + ptr))
                    written = (int32_t)needed;
            }
        }
    }
    if (nresults > 0)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = written;
    }
    return NULL;
}

static wasm_trap_t *host_draw_rect(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float x = args[0].of.f32;
        float y = args[1].of.f32;
        float w = args[2].of.f32;
        float h = args[3].of.f32;
        skia_canvas_draw_rect(CANVAS_CALLBACK_CONTEXT->canvas, x, y, x + w, y + h, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_draw_round_rect(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float x = args[0].of.f32;
        float y = args[1].of.f32;
        float w = args[2].of.f32;
        float h = args[3].of.f32;
        float rx = args[4].of.f32;
        float ry = args[5].of.f32;
        skia_canvas_draw_round_rect(CANVAS_CALLBACK_CONTEXT->canvas, x, y, x + w, y + h, rx, ry, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_draw_circle(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float cx = args[0].of.f32;
        float cy = args[1].of.f32;
        float radius = args[2].of.f32;
        skia_canvas_draw_circle(CANVAS_CALLBACK_CONTEXT->canvas, cx, cy, radius, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_draw_oval(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float x = args[0].of.f32;
        float y = args[1].of.f32;
        float w = args[2].of.f32;
        float h = args[3].of.f32;
        skia_canvas_draw_oval(CANVAS_CALLBACK_CONTEXT->canvas, x, y, x + w, y + h, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_draw_line(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float x1 = args[0].of.f32;
        float y1 = args[1].of.f32;
        float x2 = args[2].of.f32;
        float y2 = args[3].of.f32;
        skia_canvas_draw_line(CANVAS_CALLBACK_CONTEXT->canvas, x1, y1, x2, y2, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_draw_point(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float x = args[0].of.f32;
        float y = args[1].of.f32;
        skia_canvas_draw_point(CANVAS_CALLBACK_CONTEXT->canvas, x, y, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_draw_arc(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        float x = args[0].of.f32;
        float y = args[1].of.f32;
        float w = args[2].of.f32;
        float h = args[3].of.f32;
        float start_angle = args[4].of.f32;
        float sweep_angle = args[5].of.f32;
        int use_center = args[6].of.i32;
        skia_canvas_draw_arc(CANVAS_CALLBACK_CONTEXT->canvas, x, y, x + w, y + h,
                             start_angle, sweep_angle, use_center != 0, CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_draw_text(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    float width = 0.0f;
    char text[4096];
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas &&
        read_wasm_string(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32, text, sizeof(text)))
    {
        float x = args[2].of.f32;
        float y = args[3].of.f32;
        float font_size = args[4].of.f32;
        width = skia_canvas_draw_text(CANVAS_CALLBACK_CONTEXT->canvas, text, x, y, font_size,
                                      CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = width;
    return NULL;
}

static wasm_trap_t *host_canvas_measure_text(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    float width = 0.0f;
    char text[4096];
    if (CANVAS_CALLBACK_CONTEXT &&
        read_wasm_string(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32, text, sizeof(text)))
    {
        float font_size = args[2].of.f32;
        width = skia_measure_text_width(text, font_size, NULL);
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = width;
    return NULL;
}

static wasm_trap_t *host_canvas_measure_text_rect(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char text[4096];
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->has_memory &&
        read_wasm_string(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32, text, sizeof(text)))
    {
        float font_size = args[2].of.f32;
        int32_t out_w_ptr = args[3].of.i32;
        int32_t out_h_ptr = args[4].of.i32;
        float w = 0.0f, h = 0.0f;
        skia_measure_text_rect(text, font_size, NULL, &w, &h);
        uint8_t *mem = wasmtime_memory_data(CANVAS_CALLBACK_CONTEXT->context, &CANVAS_CALLBACK_CONTEXT->memory);
        size_t mem_size = wasmtime_memory_data_size(CANVAS_CALLBACK_CONTEXT->context, &CANVAS_CALLBACK_CONTEXT->memory);
        if (out_w_ptr >= 0 && (size_t)(out_w_ptr + 4) <= mem_size)
            memcpy(mem + out_w_ptr, &w, sizeof(float));
        if (out_h_ptr >= 0 && (size_t)(out_h_ptr + 4) <= mem_size)
            memcpy(mem + out_h_ptr, &h, sizeof(float));
    }
    return NULL;
}

static wasm_trap_t *host_set_fill_color(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        uint32_t color = args[0].of.i32;
        skia_paint_set_color(CANVAS_CALLBACK_CONTEXT->active_paint, color);
        skia_paint_set_style(CANVAS_CALLBACK_CONTEXT->active_paint, SKIA_PAINT_FILL);
    }
    return NULL;
}

static wasm_trap_t *host_set_stroke_color(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        uint32_t color = args[0].of.i32;
        skia_paint_set_color(CANVAS_CALLBACK_CONTEXT->active_paint, color);
        skia_paint_set_style(CANVAS_CALLBACK_CONTEXT->active_paint, SKIA_PAINT_STROKE);
    }
    return NULL;
}

static wasm_trap_t *host_set_stroke_width(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        float width = args[0].of.f32;
        skia_paint_set_stroke_width(CANVAS_CALLBACK_CONTEXT->active_paint, width);
    }
    return NULL;
}

static wasm_trap_t *host_set_anti_alias(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        int enabled = args[0].of.i32;
        skia_paint_set_anti_alias(CANVAS_CALLBACK_CONTEXT->active_paint, enabled != 0);
    }
    return NULL;
}

static wasm_trap_t *host_set_alpha(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        int alpha = args[0].of.i32;
        if (alpha < 0)
            alpha = 0;
        if (alpha > 255)
            alpha = 255;
        skia_paint_set_alpha(CANVAS_CALLBACK_CONTEXT->active_paint, (uint8_t)alpha);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_set_blend_mode(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        int mode = args[0].of.i32;
        skia_paint_set_blend_mode(CANVAS_CALLBACK_CONTEXT->active_paint, (SkiaBlendMode)mode);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_set_blur_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        skia_paint_set_blur_filter(CANVAS_CALLBACK_CONTEXT->active_paint,
                                   args[0].of.f32, args[1].of.f32);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_set_drop_shadow_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        skia_paint_set_drop_shadow_filter(CANVAS_CALLBACK_CONTEXT->active_paint,
                                          args[0].of.f32, args[1].of.f32,
                                          args[2].of.f32, args[3].of.f32,
                                          (uint32_t)args[4].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_set_drop_shadow_only_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        skia_paint_set_drop_shadow_only_filter(CANVAS_CALLBACK_CONTEXT->active_paint,
                                               args[0].of.f32, args[1].of.f32,
                                               args[2].of.f32, args[3].of.f32,
                                               (uint32_t)args[4].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_clear_image_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        skia_paint_clear_image_filter(CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_set_color_matrix_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (!CANVAS_CALLBACK_CONTEXT || !CANVAS_CALLBACK_CONTEXT->active_paint || !CANVAS_CALLBACK_CONTEXT->has_memory)
        return NULL;
    int32_t ptr = args[0].of.i32;
    int32_t len = args[1].of.i32;
    if (len != 20 || ptr < 0)
        return NULL;
    uint8_t *mem = wasmtime_memory_data(CANVAS_CALLBACK_CONTEXT->context, &CANVAS_CALLBACK_CONTEXT->memory);
    size_t mem_size = wasmtime_memory_data_size(CANVAS_CALLBACK_CONTEXT->context, &CANVAS_CALLBACK_CONTEXT->memory);
    if ((size_t)ptr + 20 * sizeof(float) > mem_size)
        return NULL;
    float m[20];
    memcpy(m, mem + ptr, sizeof(m));
    skia_paint_set_color_matrix_filter(CANVAS_CALLBACK_CONTEXT->active_paint, m);
    return NULL;
}

static wasm_trap_t *host_canvas_set_blend_color_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        uint32_t color = (uint32_t)args[0].of.i32;
        int mode = args[1].of.i32;
        skia_paint_set_blend_color_filter(CANVAS_CALLBACK_CONTEXT->active_paint,
                                          color, (SkiaBlendMode)mode);
    }
    return NULL;
}

static wasm_trap_t *host_canvas_clear_color_filter(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    (void)results;
    (void)nresults;
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->active_paint)
    {
        skia_paint_clear_color_filter(CANVAS_CALLBACK_CONTEXT->active_paint);
    }
    return NULL;
}

static wasm_trap_t *host_path_create(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int id = -1;
    if (CANVAS_CALLBACK_CONTEXT)
    {
        SkiaPath *path = skia_path_create();
        if (path)
        {
            id = add_path(CANVAS_CALLBACK_CONTEXT, path);
            if (id < 0)
            {
                skia_path_destroy(path);
            }
        }
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = id;
    return NULL;
}

static wasm_trap_t *host_path_reset(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_reset(path);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_move_to(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        float x = args[1].of.f32;
        float y = args[2].of.f32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_move_to(path, x, y);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_line_to(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        float x = args[1].of.f32;
        float y = args[2].of.f32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_line_to(path, x, y);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_quad_to(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        float x1 = args[1].of.f32;
        float y1 = args[2].of.f32;
        float x2 = args[3].of.f32;
        float y2 = args[4].of.f32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_quad_to(path, x1, y1, x2, y2);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_cubic_to(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        float x1 = args[1].of.f32;
        float y1 = args[2].of.f32;
        float x2 = args[3].of.f32;
        float y2 = args[4].of.f32;
        float x3 = args[5].of.f32;
        float y3 = args[6].of.f32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_cubic_to(path, x1, y1, x2, y2, x3, y3);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_close(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_close(path);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_add_rect(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        float x = args[1].of.f32;
        float y = args[2].of.f32;
        float w = args[3].of.f32;
        float h = args[4].of.f32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_add_rect(path, x, y, x + w, y + h);
        }
    }
    return NULL;
}

static wasm_trap_t *host_path_add_circle(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        float cx = args[1].of.f32;
        float cy = args[2].of.f32;
        float radius = args[3].of.f32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_path_add_circle(path, cx, cy, radius);
        }
    }
    return NULL;
}

static wasm_trap_t *host_draw_path(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        int id = args[0].of.i32;
        SkiaPath *path = get_path(CANVAS_CALLBACK_CONTEXT, id);
        if (path)
        {
            skia_canvas_draw_path(CANVAS_CALLBACK_CONTEXT->canvas, path, CANVAS_CALLBACK_CONTEXT->active_paint);
        }
    }
    return NULL;
}

static wasm_trap_t *host_get_width(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ctx ? ctx->width : 0;
    return NULL;
}

static wasm_trap_t *host_get_height(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ctx ? ctx->height : 0;
    return NULL;
}

static wasm_trap_t *host_get_display_density(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = ctx ? ctx->display_density : 1.0f;
    return NULL;
}

static wasm_trap_t *host_get_mouse_x(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = (ctx && ctx->input) ? ctx->input->mouse_x : 0;
    return NULL;
}

static wasm_trap_t *host_get_mouse_y(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = (ctx && ctx->input) ? ctx->input->mouse_y : 0;
    return NULL;
}

static wasm_trap_t *host_get_mouse_button(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    int button = args[0].of.i32;
    int pressed = 0;
    if (ctx && ctx->input && button >= 0 && button < 3)
    {
        pressed = ctx->input->mouse_buttons[button] ? 1 : 0;
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = pressed;
    return NULL;
}

static wasm_trap_t *host_get_key_down(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    int scancode = args[0].of.i32;
    int down = 0;
    if (ctx && ctx->input)
    {
        down = input_key_down(ctx->input, scancode) ? 1 : 0;
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = down;
    return NULL;
}

static wasm_trap_t *host_get_delta_time(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = (ctx && ctx->input) ? (float)ctx->input->delta_time : 0.0f;
    return NULL;
}

static wasm_trap_t *host_get_total_time(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)env;
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = (ctx && ctx->input) ? (float)ctx->input->total_time : 0.0f;
    return NULL;
}

static wasm_trap_t *host_log_int(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    printf("%d\n", args[0].of.i32);
    return NULL;
}

static wasm_trap_t *host_log_float(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    printf("%f\n", args[0].of.f32);
    return NULL;
}

static wasm_trap_t *host_sin(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = sinf(args[0].of.f32);
    return NULL;
}

static wasm_trap_t *host_cos(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = cosf(args[0].of.f32);
    return NULL;
}

static wasm_trap_t *host_sqrt(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = sqrtf(args[0].of.f32);
    return NULL;
}

static wasm_trap_t *host_atan2(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = atan2f(args[0].of.f32, args[1].of.f32);
    return NULL;
}

static int add_svg(WasmCanvasContext *ctx, SkiaSVG *svg)
{
    if (ctx->svg_count >= MAX_PATHS)
        return -1;
    ctx->svgs[ctx->svg_count] = svg;
    return ctx->svg_count++;
}

static SkiaSVG *get_svg_wasm(WasmCanvasContext *ctx, int id)
{
    if (id < 0 || id >= ctx->svg_count)
        return NULL;
    return ctx->svgs[id];
}

static wasm_trap_t *host_svg_load(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char path[MAX_WASM_STRING];
    char resolved[1024];
    int svg_id = -1;

    if (CANVAS_CALLBACK_CONTEXT &&
        read_wasm_string(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32, path, sizeof(path)))
    {
        
        if (path[0] != '/' && CANVAS_CALLBACK_CONTEXT->project_dir[0])
        {
            snprintf(resolved, sizeof(resolved), "%s/%s", CANVAS_CALLBACK_CONTEXT->project_dir, path);
        }
        else
        {
            snprintf(resolved, sizeof(resolved), "%s", path);
        }

        SkiaSVG *svg = skia_svg_load_file(resolved);
        if (svg)
        {
            svg_id = add_svg(CANVAS_CALLBACK_CONTEXT, svg);
            if (svg_id < 0)
            {
                skia_svg_destroy(svg);
            }
        }
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = svg_id;
    return NULL;
}

static wasm_trap_t *host_svg_load_from_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (CANVAS_CALLBACK_CONTEXT && nargs >= 2)
    {
        const uint8_t *data = read_wasm_bytes(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32);
        if (data)
        {
            SkiaSVG *svg = skia_svg_load_buffer(data, (size_t)args[1].of.i32);
            if (svg)
            {
                int id = add_svg(CANVAS_CALLBACK_CONTEXT, svg);
                if (id >= 0)
                    results[0].of.i32 = id;
                else
                    skia_svg_destroy(svg);
            }
        }
    }
    return NULL;
}

static wasm_trap_t *host_svg_destroy(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT)
    {
        int id = args[0].of.i32;
        SkiaSVG *svg = get_svg_wasm(CANVAS_CALLBACK_CONTEXT, id);
        if (svg)
        {
            skia_svg_destroy(svg);
            CANVAS_CALLBACK_CONTEXT->svgs[id] = NULL;
        }
    }
    return NULL;
}

static wasm_trap_t *host_svg_draw(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && CANVAS_CALLBACK_CONTEXT->canvas)
    {
        int id = args[0].of.i32;
        float x = args[1].of.f32;
        float y = args[2].of.f32;
        float w = args[3].of.f32;
        float h = args[4].of.f32;

        SkiaSVG *svg = get_svg_wasm(CANVAS_CALLBACK_CONTEXT, id);
        if (svg)
        {
            skia_svg_render(svg, CANVAS_CALLBACK_CONTEXT->canvas, x, y, w, h);
        }
    }
    return NULL;
}

static wasm_trap_t *host_svg_get_width(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    float w = 0.0f;
    if (CANVAS_CALLBACK_CONTEXT)
    {
        SkiaSVG *svg = get_svg_wasm(CANVAS_CALLBACK_CONTEXT, args[0].of.i32);
        if (svg)
            w = skia_svg_get_width(svg);
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = w;
    return NULL;
}

static wasm_trap_t *host_svg_get_height(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    float h = 0.0f;
    if (CANVAS_CALLBACK_CONTEXT)
    {
        SkiaSVG *svg = get_svg_wasm(CANVAS_CALLBACK_CONTEXT, args[0].of.i32);
        if (svg)
            h = skia_svg_get_height(svg);
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = h;
    return NULL;
}

#undef CANVAS_CALLBACK_CONTEXT

#include <math.h>

typedef struct
{
    const char *name;
    wasmtime_func_callback_t callback;
    wasm_valtype_t *params[8];
    int param_count;
    wasm_valtype_t *results[2];
    int result_count;
} HostFuncDef;

static wasmtime_error_t *define_canvas_host_function(
    WasmCanvasContext *ctx,
    wasmtime_linker_t *linker,
    const char *module,
    const char *name,
    wasmtime_func_callback_t callback,
    const wasm_valkind_t *param_types, size_t param_count,
    const wasm_valkind_t *result_types, size_t result_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t i = 0; i < param_count; i++)
    {
        params.data[i] = wasm_valtype_new(param_types[i]);
    }
    wasm_valtype_vec_new_uninitialized(&results, result_count);
    for (size_t i = 0; i < result_count; i++)
    {
        results.data[i] = wasm_valtype_new(result_types[i]);
    }

    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, module, strlen(module), name, strlen(name),
        functype, callback, ctx, NULL);
    wasm_functype_delete(functype);

    return error;
}

SkiaCanvas *wasm_canvas_current_canvas(WasmCanvasContext *ctx)
{
    return ctx ? ctx->canvas : NULL;
}

Window *wasm_canvas_current_window(WasmCanvasContext *ctx)
{
    return ctx ? ctx->window : NULL;
}

bool wasm_canvas_read_string(WasmCanvasContext *ctx, int32_t pointer,
                             int32_t length, char *output,
                             size_t output_size)
{
    return read_wasm_string(ctx, pointer, length, output, output_size);
}

const uint8_t *wasm_canvas_read_bytes(WasmCanvasContext *ctx,
                                      int32_t pointer, int32_t length)
{
    return read_wasm_bytes(ctx, pointer, length);
}

void wasm_canvas_set_error(WasmCanvasContext *ctx, const char *message)
{
    set_error(ctx, message);
}

WasmCanvasContext *wasm_canvas_create(const char *project_dir)
{
    WasmCanvasContext *ctx = (WasmCanvasContext *)calloc(1, sizeof(WasmCanvasContext));
    if (!ctx)
        return NULL;

    if (project_dir)
    {
        strncpy(ctx->project_dir, project_dir, sizeof(ctx->project_dir) - 1);
        ctx->project_dir[sizeof(ctx->project_dir) - 1] = '\0';
    }

    {
        wasm_config_t *config = wasm_config_new();
        wasmtime_config_cranelift_opt_level_set(config, WASMTIME_OPT_LEVEL_NONE);
        ctx->engine = wasm_engine_new_with_config(config);
    }
    if (!ctx->engine)
    {
        free(ctx);
        return NULL;
    }

    ctx->store = wasmtime_store_new(ctx->engine, NULL, NULL);
    if (!ctx->store)
    {
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->context = wasmtime_store_context(ctx->store);

    ctx->linker = wasmtime_linker_new(ctx->engine);
    if (!ctx->linker)
    {
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->active_paint = skia_paint_create();
    if (!ctx->active_paint)
    {
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->audio_binding_state = wasm_audio_binding_state_create(project_dir);
    if (!ctx->audio_binding_state)
    {
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->math_binding_state = wasm_math_binding_state_create();
    if (!ctx->math_binding_state)
    {
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->sqlite_binding_state = wasm_sqlite_binding_state_create(project_dir);
    if (!ctx->sqlite_binding_state)
    {
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->file_binding_state = wasm_file_binding_state_create(project_dir);
    if (!ctx->file_binding_state)
    {
        wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->magneto_binding_state = wasm_magneto_binding_state_create();
    if (!ctx->magneto_binding_state)
    {
        wasm_file_binding_state_destroy(ctx->file_binding_state);
        wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

#ifdef BUDO_NEURAL
    ctx->neural_binding_state = wasm_neural_binding_state_create();
    if (!ctx->neural_binding_state)
    {
        wasm_magneto_binding_state_destroy(ctx->magneto_binding_state);
        wasm_file_binding_state_destroy(ctx->file_binding_state);
        wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }
#endif

    ctx->udp_binding_state = wasm_udp_binding_state_create();
    if (!ctx->udp_binding_state)
    {
#ifdef BUDO_NEURAL
        wasm_neural_binding_state_destroy(ctx->neural_binding_state);
#endif
        wasm_magneto_binding_state_destroy(ctx->magneto_binding_state);
        wasm_file_binding_state_destroy(ctx->file_binding_state);
        wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->midi_binding_state = wasm_midi_binding_state_create();
    if (!ctx->midi_binding_state)
    {
        wasm_udp_binding_state_destroy(ctx->udp_binding_state);
#ifdef BUDO_NEURAL
        wasm_neural_binding_state_destroy(ctx->neural_binding_state);
#endif
        wasm_magneto_binding_state_destroy(ctx->magneto_binding_state);
        wasm_file_binding_state_destroy(ctx->file_binding_state);
        wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }

    ctx->network_binding_state =
        wasm_network_binding_state_create(project_dir);
    if (!ctx->network_binding_state)
    {
        wasm_midi_binding_state_destroy(ctx->midi_binding_state);
        wasm_udp_binding_state_destroy(ctx->udp_binding_state);
#ifdef BUDO_NEURAL
        wasm_neural_binding_state_destroy(ctx->neural_binding_state);
#endif
        wasm_magneto_binding_state_destroy(ctx->magneto_binding_state);
        wasm_file_binding_state_destroy(ctx->file_binding_state);
        wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
        wasm_math_binding_state_destroy(ctx->math_binding_state);
        wasm_audio_binding_state_destroy(ctx->audio_binding_state);
        skia_paint_destroy(ctx->active_paint);
        wasmtime_linker_delete(ctx->linker);
        wasmtime_store_delete(ctx->store);
        wasm_engine_delete(ctx->engine);
        free(ctx);
        return NULL;
    }
    ctx->rtpmidi_ctx = rtpmidi_create(wasm_udp_context(ctx->udp_binding_state));
    wasm_midi_set_rtpmidi(ctx->midi_binding_state, ctx->rtpmidi_ctx);

    wasmtime_error_t *error = NULL;

#define define_host_function(...) define_canvas_host_function(ctx, __VA_ARGS__)

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {};
        error = define_host_function(ctx->linker, "env", "canvas_clear", host_canvas_clear, params, 1, results, 0);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_read_pixels", host_canvas_read_pixels, params, 2, results, 1);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_rect", host_draw_rect, params, 4, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_round_rect", host_draw_round_rect, params, 6, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_circle", host_draw_circle, params, 3, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_oval", host_draw_oval, params, 4, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_line", host_draw_line, params, 4, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_point", host_draw_point, params, 2, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_arc", host_draw_arc, params, 7, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_F32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_text", host_canvas_draw_text, params, 5, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_F32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_measure_text", host_canvas_measure_text, params, 3, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_F32, WASM_I32, WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_measure_text_rect", host_canvas_measure_text_rect, params, 5, NULL, 0);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_set_fill_color", host_set_fill_color, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_set_stroke_color", host_set_stroke_color, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
        {
            wasm_valkind_t params[] = {WASM_I32};
            error = define_host_function(ctx->linker, "env", "canvas_set_blend_mode", host_canvas_set_blend_mode, params, 1, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            wasm_valkind_t params[] = {WASM_F32, WASM_F32};
            error = define_host_function(ctx->linker, "env", "canvas_set_blur_filter", host_canvas_set_blur_filter, params, 2, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_I32};
            error = define_host_function(ctx->linker, "env", "canvas_set_drop_shadow_filter", host_canvas_set_drop_shadow_filter, params, 5, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            wasm_valkind_t params[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_I32};
            error = define_host_function(ctx->linker, "env", "canvas_set_drop_shadow_only_filter", host_canvas_set_drop_shadow_only_filter, params, 5, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            error = define_host_function(ctx->linker, "env", "canvas_clear_image_filter", host_canvas_clear_image_filter, NULL, 0, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            
            wasm_valkind_t params[] = {WASM_I32, WASM_I32};
            error = define_host_function(ctx->linker, "env", "canvas_set_color_matrix_filter", host_canvas_set_color_matrix_filter, params, 2, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            
            wasm_valkind_t params[] = {WASM_I32, WASM_I32};
            error = define_host_function(ctx->linker, "env", "canvas_set_blend_color_filter", host_canvas_set_blend_color_filter, params, 2, NULL, 0);
            if (error)
                goto error_cleanup;
        }
        {
            error = define_host_function(ctx->linker, "env", "canvas_clear_color_filter", host_canvas_clear_color_filter, NULL, 0, NULL, 0);
            if (error)
                goto error_cleanup;
        }
    }
    {
        wasm_valkind_t params[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_set_stroke_width", host_set_stroke_width, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_set_anti_alias", host_set_anti_alias, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_set_alpha", host_set_alpha, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_path", host_draw_path, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "canvas_draw_svg", host_svg_draw, params, 5, NULL, 0);
        if (error)
            goto error_cleanup;
    }

    error = wasm_transform_register(ctx->linker, ctx);
    if (error)
        goto error_cleanup;

    error = wasm_gl_register(ctx->linker, ctx);
    if (error)
        goto error_cleanup;

    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "path_create", host_path_create, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "path_reset", host_path_reset, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "path_move_to", host_path_move_to, params, 3, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "path_line_to", host_path_line_to, params, 3, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "path_quad_to", host_path_quad_to, params, 5, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "path_cubic_to", host_path_cubic_to, params, 7, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "path_close", host_path_close, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "path_add_rect", host_path_add_rect, params, 5, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(ctx->linker, "env", "path_add_circle", host_path_add_circle, params, 4, NULL, 0);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "svg_load", host_svg_load, params, 2, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "svg_load_from_buffer", host_svg_load_from_buffer, params, 2, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "svg_destroy", host_svg_destroy, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "svg_get_width", host_svg_get_width, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "svg_get_height", host_svg_get_height, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "window_get_width", host_get_width, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "window_get_height", host_get_height, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "window_get_display_density", host_get_display_density, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "input_get_mouse_x", host_get_mouse_x, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "input_get_mouse_y", host_get_mouse_y, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "input_get_mouse_button", host_get_mouse_button, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "input_get_key_down", host_get_key_down, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "input_get_delta_time", host_get_delta_time, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "input_get_total_time", host_get_total_time, NULL, 0, results, 1);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(ctx->linker, "env", "log_int", host_log_int, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "log_float", host_log_float, params, 1, NULL, 0);
        if (error)
            goto error_cleanup;
    }

    {
        wasm_valkind_t params[] = {WASM_F32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "sin", host_sin, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "cos", host_cos, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "sqrt", host_sqrt, params, 1, results, 1);
        if (error)
            goto error_cleanup;
    }
    {
        wasm_valkind_t params[] = {WASM_F32, WASM_F32};
        wasm_valkind_t results[] = {WASM_F32};
        error = define_host_function(ctx->linker, "env", "atan2", host_atan2, params, 2, results, 1);
        if (error)
            goto error_cleanup;
    }

    error = wasm_audio_register(ctx->linker, ctx->audio_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_midi_register(ctx->linker, ctx->midi_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_sqlite_register(ctx->linker, ctx->sqlite_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_file_register_state(ctx->linker, ctx->file_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_magneto_register(ctx->linker, ctx->magneto_binding_state);
    if (error)
        goto error_cleanup;

    ctx->device_binding_state = device_binding_state_create();
    if (!ctx->device_binding_state)
    {
        error = wasmtime_error_new("Failed to create WASM device binding state");
        goto error_cleanup;
    }

    error = wasm_device_register(ctx->linker, ctx->device_binding_state);
    if (error)
        goto error_cleanup;

#ifdef BUDO_NEURAL
    error = wasm_neural_register(ctx->linker, ctx->neural_binding_state);
    if (error)
        goto error_cleanup;
#endif

    error = wasm_math_register(ctx->linker, ctx->math_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_network_register_state(ctx->linker,
                                        ctx->network_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_udp_register(ctx->linker, ctx->udp_binding_state);
    if (error)
        goto error_cleanup;

    error = wasm_capabilities_register(ctx->linker);
    if (error)
        goto error_cleanup;

    return ctx;

error_cleanup:
    if (error)
    {
        set_wasmtime_error(ctx, error, NULL);
        fprintf(stderr, "Failed to define host functions: %s\n", ctx->error_msg);
    }
    wasm_network_shutdown(ctx->network_binding_state);
    skia_paint_destroy(ctx->active_paint);
    wasmtime_linker_delete(ctx->linker);
    wasm_midi_clear_memory(ctx->midi_binding_state);
    wasm_udp_clear_memory(ctx->udp_binding_state);
    device_binding_state_destroy(ctx->device_binding_state);
    ctx->device_binding_state = NULL;
    wasmtime_store_delete(ctx->store);
    wasm_midi_set_rtpmidi(ctx->midi_binding_state, NULL);
    wasm_midi_binding_state_destroy(ctx->midi_binding_state);
    rtpmidi_destroy(ctx->rtpmidi_ctx);
    wasm_udp_binding_state_destroy(ctx->udp_binding_state);
    wasm_network_binding_state_destroy(ctx->network_binding_state);
#ifdef BUDO_NEURAL
    wasm_neural_binding_state_destroy(ctx->neural_binding_state);
#endif
    wasm_magneto_binding_state_destroy(ctx->magneto_binding_state);
    wasm_file_binding_state_destroy(ctx->file_binding_state);
    wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
    wasm_math_binding_state_destroy(ctx->math_binding_state);
    wasm_audio_binding_state_destroy(ctx->audio_binding_state);
    wasm_engine_delete(ctx->engine);
    free(ctx);
    return NULL;
}

#ifdef BUDO_NEURAL
bool wasm_canvas_enable_neural(WasmCanvasContext *ctx)
{
    return ctx && wasm_neural_binding_state_enable(ctx->neural_binding_state,
                                                   ctx->project_dir);
}
#endif

void wasm_canvas_destroy(WasmCanvasContext *ctx)
{
    if (!ctx)
        return;

    wasm_network_shutdown(ctx->network_binding_state);

    for (int i = 0; i < ctx->path_count; i++)
    {
        if (ctx->paths[i])
        {
            skia_path_destroy(ctx->paths[i]);
        }
    }

    for (int i = 0; i < ctx->svg_count; i++)
    {
        if (ctx->svgs[i])
        {
            skia_svg_destroy(ctx->svgs[i]);
        }
    }

    if (ctx->module)
    {
        wasmtime_module_delete(ctx->module);
    }

    if (ctx->active_paint)
    {
        skia_paint_destroy(ctx->active_paint);
    }

    if (ctx->linker)
    {
        wasmtime_linker_delete(ctx->linker);
    }

    wasm_midi_clear_memory(ctx->midi_binding_state);
    wasm_udp_clear_memory(ctx->udp_binding_state);
    device_binding_state_destroy(ctx->device_binding_state);
    ctx->device_binding_state = NULL;
    if (ctx->store)
    {
        wasmtime_store_delete(ctx->store);
    }

    wasm_midi_set_rtpmidi(ctx->midi_binding_state, NULL);
    wasm_midi_binding_state_destroy(ctx->midi_binding_state);
    rtpmidi_destroy(ctx->rtpmidi_ctx);
    wasm_udp_binding_state_destroy(ctx->udp_binding_state);
    wasm_network_binding_state_destroy(ctx->network_binding_state);
#ifdef BUDO_NEURAL
    wasm_neural_binding_state_destroy(ctx->neural_binding_state);
#endif
    wasm_math_binding_state_destroy(ctx->math_binding_state);
    wasm_sqlite_binding_state_destroy(ctx->sqlite_binding_state);
    wasm_file_binding_state_destroy(ctx->file_binding_state);
    wasm_magneto_binding_state_destroy(ctx->magneto_binding_state);
    wasm_audio_binding_state_destroy(ctx->audio_binding_state);

    if (ctx->engine)
    {
        wasm_engine_delete(ctx->engine);
    }

    free(ctx);
}

UdpContext *wasm_canvas_udp_context(WasmCanvasContext *ctx)
{
    return ctx ? wasm_udp_context(ctx->udp_binding_state) : NULL;
}

void wasm_canvas_midi_poll(WasmCanvasContext *ctx)
{
    if (ctx)
        wasm_midi_poll(ctx->midi_binding_state);
}

void wasm_canvas_network_poll(WasmCanvasContext *ctx)
{
    if (ctx)
        wasm_network_poll(ctx->network_binding_state);
}

FileContext *wasm_canvas_file_context(WasmCanvasContext *ctx)
{
    return ctx ? wasm_file_context(ctx->file_binding_state) : NULL;
}

static bool load_file_contents(const char *filename, wasm_byte_vec_t *out)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    wasm_byte_vec_new_uninitialized(out, size);
    size_t read = fread(out->data, 1, size, file);
    fclose(file);

    if (read != (size_t)size)
    {
        wasm_byte_vec_delete(out);
        return false;
    }

    return true;
}

static void initialize_wasm_memory_exports(WasmCanvasContext *ctx)
{
    wasmtime_extern_t ext;

    ctx->has_memory = wasmtime_instance_export_get(ctx->context, &ctx->instance, "memory", 6, &ext);
    if (ctx->has_memory && ext.kind == WASMTIME_EXTERN_MEMORY)
    {
        ctx->memory = ext.of.memory;
    }
    else
    {
        ctx->has_memory = false;
    }

    if (ctx->has_memory)
    {
        wasm_sqlite_set_memory(ctx->sqlite_binding_state, ctx->context, &ctx->memory);
        wasm_file_binding_state_set_memory(ctx->file_binding_state,
                                           ctx->context, &ctx->memory);
        wasm_midi_set_memory(ctx->midi_binding_state,
                             ctx->context, &ctx->memory);
#ifdef BUDO_NEURAL
        wasm_neural_binding_state_set_memory(ctx->neural_binding_state,
                                             ctx->context, &ctx->memory);
#endif
        wasm_math_set_memory(ctx->math_binding_state, ctx->context, &ctx->memory);
        wasm_network_binding_state_set_memory(ctx->network_binding_state,
                                              ctx->context, &ctx->memory);
        wasm_udp_set_memory(ctx->udp_binding_state, ctx->context, &ctx->memory);
    }
}

bool wasm_canvas_load_wat_file(WasmCanvasContext *ctx, const char *filename)
{
    if (!ctx || !filename)
        return false;

    wasm_byte_vec_t wat;
    if (!load_file_contents(filename, &wat))
    {
        set_error(ctx, "Failed to read WAT file");
        return false;
    }

    wasm_byte_vec_t wasm;
    wasmtime_error_t *error = wasmtime_wat2wasm(wat.data, wat.size, &wasm);
    wasm_byte_vec_delete(&wat);

    if (error)
    {
        set_wasmtime_error(ctx, error, NULL);
        return false;
    }

    error = wasmtime_module_new(ctx->engine, (uint8_t *)wasm.data, wasm.size, &ctx->module);
    wasm_byte_vec_delete(&wasm);

    if (error)
    {
        set_wasmtime_error(ctx, error, NULL);
        return false;
    }

    wasm_trap_t *trap = NULL;
    error = wasmtime_linker_instantiate(ctx->linker, ctx->context, ctx->module, &ctx->instance, &trap);

    if (error || trap)
    {
        set_wasmtime_error(ctx, error, trap);
        return false;
    }

    wasmtime_extern_t ext;

    initialize_wasm_memory_exports(ctx);

    ctx->has_init_func = wasmtime_instance_export_get(ctx->context, &ctx->instance, "init", 4, &ext);
    if (ctx->has_init_func && ext.kind == WASMTIME_EXTERN_FUNC)
    {
        ctx->init_func = ext.of.func;

        error = wasmtime_func_call(ctx->context, &ctx->init_func, NULL, 0, NULL, 0, &trap);
        if (error || trap)
        {
            set_wasmtime_error(ctx, error, trap);
            fprintf(stderr, "Warning: init function failed: %s\n", ctx->error_msg);
        }
    }

    ctx->has_frame_func = wasmtime_instance_export_get(ctx->context, &ctx->instance, "frame", 5, &ext);
    if (ctx->has_frame_func && ext.kind == WASMTIME_EXTERN_FUNC)
    {
        ctx->frame_func = ext.of.func;
    }
    else
    {
        ctx->has_frame_func = false;
    }

    return true;
}

bool wasm_canvas_load_wasm_file(WasmCanvasContext *ctx, const char *filename)
{
    if (!ctx || !filename)
        return false;

    wasm_byte_vec_t wasm;
    if (!load_file_contents(filename, &wasm))
    {
        set_error(ctx, "Failed to read WASM file");
        return false;
    }

    wasmtime_error_t *error = wasmtime_module_new(ctx->engine, (uint8_t *)wasm.data, wasm.size, &ctx->module);
    wasm_byte_vec_delete(&wasm);

    if (error)
    {
        set_wasmtime_error(ctx, error, NULL);
        return false;
    }

    wasm_trap_t *trap = NULL;
    error = wasmtime_linker_instantiate(ctx->linker, ctx->context, ctx->module, &ctx->instance, &trap);

    if (error || trap)
    {
        set_wasmtime_error(ctx, error, trap);
        return false;
    }

    wasmtime_extern_t ext;

    initialize_wasm_memory_exports(ctx);

    ctx->has_init_func = wasmtime_instance_export_get(ctx->context, &ctx->instance, "init", 4, &ext);
    if (ctx->has_init_func && ext.kind == WASMTIME_EXTERN_FUNC)
    {
        ctx->init_func = ext.of.func;

        error = wasmtime_func_call(ctx->context, &ctx->init_func, NULL, 0, NULL, 0, &trap);
        if (error || trap)
        {
            set_wasmtime_error(ctx, error, trap);
            fprintf(stderr, "Warning: init function failed: %s\n", ctx->error_msg);
        }
    }

    ctx->has_frame_func = wasmtime_instance_export_get(ctx->context, &ctx->instance, "frame", 5, &ext);
    if (ctx->has_frame_func && ext.kind == WASMTIME_EXTERN_FUNC)
    {
        ctx->frame_func = ext.of.func;
    }
    else
    {
        ctx->has_frame_func = false;
    }

    return true;
}

void wasm_canvas_set_context(WasmCanvasContext *ctx, SkiaCanvas *canvas,
                             InputState *input, Window *window, int width, int height,
                             float display_density)
{
    if (!ctx)
        return;
    ctx->canvas = canvas;
    ctx->input = input;
    ctx->window = window;
    ctx->width = width;
    ctx->height = height;
    ctx->display_density = display_density > 0.0f ? display_density : 1.0f;
}

bool wasm_canvas_call_animation(WasmCanvasContext *ctx, double timestamp)
{
    if (!ctx || !ctx->has_frame_func)
        return false;

    wasmtime_val_t args[1];
    args[0].kind = WASMTIME_F32;
    args[0].of.f32 = (float)timestamp;

    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(ctx->context, &ctx->frame_func, args, 1, NULL, 0, &trap);

    if (error || trap)
    {
        set_wasmtime_error(ctx, error, trap);
        fprintf(stderr, "Frame function error: %s\n", ctx->error_msg);
        return false;
    }

    return true;
}

bool wasm_canvas_has_animation(WasmCanvasContext *ctx)
{
    return ctx && ctx->has_frame_func;
}

const char *wasm_canvas_get_error(WasmCanvasContext *ctx)
{
    return ctx ? ctx->error_msg : NULL;
}