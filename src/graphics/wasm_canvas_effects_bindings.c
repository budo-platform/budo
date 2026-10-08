#include "graphics/wasm_canvas_effects_bindings.h"

#include "graphics/wasm_canvas_bindings.h"

#include <stdlib.h>
#include <string.h>

#define HOST_ARGUMENTS void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args, size_t nargs, \
                       wasmtime_val_t *results, size_t nresults

static bool read_words(WasmCanvasContext *ctx, int32_t ptr, int count, void *out)
{
    if (count <= 0 || count > SKIA_GRADIENT_MAX_STOPS)
        return false;
    const uint8_t *bytes = wasm_canvas_guest_bytes(ctx, ptr, count * 4);
    if (!bytes)
        return false;
    memcpy(out, bytes, (size_t)count * 4u);
    return true;
}

static void set_gradient(WasmCanvasContext *ctx, int kind, const float *values,
                         int32_t colors_ptr, int32_t stops_ptr, int32_t count)
{
    uint32_t colors[SKIA_GRADIENT_MAX_STOPS];
    float stops[SKIA_GRADIENT_MAX_STOPS];
    SkiaPaint *paint = wasm_canvas_active_paint(ctx);
    if (!paint || !read_words(ctx, colors_ptr, count, colors))
        return;
    if (stops_ptr >= 0 && !read_words(ctx, stops_ptr, count, stops))
        return;
    const float *stop_values = stops_ptr >= 0 ? stops : NULL;
    if (kind == 0)
        skia_paint_set_linear_gradient(paint, values[0], values[1], values[2], values[3],
                                       colors, stop_values, count);
    else if (kind == 1)
        skia_paint_set_radial_gradient(paint, values[0], values[1], values[2], colors, stop_values, count);
    else
        skia_paint_set_sweep_gradient(paint, values[0], values[1], colors, stop_values, count);
}

static wasm_trap_t *host_set_linear_gradient(HOST_ARGUMENTS)
{
    float values[4] = {args[0].of.f32, args[1].of.f32, args[2].of.f32, args[3].of.f32};
    set_gradient(wasm_canvas_graphics(env), 0, values, args[4].of.i32, args[5].of.i32, args[6].of.i32);
    return NULL;
}

static wasm_trap_t *host_set_radial_gradient(HOST_ARGUMENTS)
{
    float values[3] = {args[0].of.f32, args[1].of.f32, args[2].of.f32};
    set_gradient(wasm_canvas_graphics(env), 1, values, args[3].of.i32, args[4].of.i32, args[5].of.i32);
    return NULL;
}

static wasm_trap_t *host_set_sweep_gradient(HOST_ARGUMENTS)
{
    float values[2] = {args[0].of.f32, args[1].of.f32};
    set_gradient(wasm_canvas_graphics(env), 2, values, args[2].of.i32, args[3].of.i32, args[4].of.i32);
    return NULL;
}

static wasm_trap_t *host_clear_gradient(HOST_ARGUMENTS)
{
    SkiaPaint *paint = wasm_canvas_active_paint(wasm_canvas_graphics(env));
    if (paint)
        skia_paint_clear_shader(paint);
    return NULL;
}

static wasm_trap_t *host_clip_path(HOST_ARGUMENTS)
{
    WasmCanvasContext *ctx = wasm_canvas_graphics(env);
    SkiaCanvas *canvas = wasm_canvas_current_canvas(ctx);
    SkiaPath *path = wasm_canvas_path(ctx, args[0].of.i32);
    if (canvas && path)
        skia_canvas_clip_path(canvas, path);
    return NULL;
}

static char *read_text(WasmCanvasContext *ctx, int32_t ptr, int32_t len)
{
    if (len == 0)
        return calloc(1, 1);
    const uint8_t *bytes = wasm_canvas_guest_bytes(ctx, ptr, len);
    char *text = bytes ? malloc((size_t)len + 1) : NULL;
    if (!text)
        return NULL;
    memcpy(text, bytes, (size_t)len);
    text[len] = '\0';
    return text;
}

static wasm_trap_t *host_draw_paragraph(HOST_ARGUMENTS)
{
    WasmCanvasContext *ctx = wasm_canvas_graphics(env);
    SkiaCanvas *canvas = wasm_canvas_current_canvas(ctx);
    SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
    char *text = canvas ? read_text(ctx, args[0].of.i32, args[1].of.i32) : NULL;
    int align = args[6].of.i32;
    if (text)
        metrics = skia_canvas_draw_paragraph(canvas, text, args[2].of.f32, args[3].of.f32, args[4].of.f32,
                                             args[5].of.f32, args[7].of.f32,
                                             align >= 0 && align <= 2 ? (SkiaTextAlign)align : SKIA_TEXT_ALIGN_LEFT,
                                             args[8].of.i32, wasm_canvas_active_paint(ctx), NULL);
    free(text);
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = metrics.height;
    return NULL;
}

static wasm_trap_t *host_measure_paragraph(HOST_ARGUMENTS)
{
    WasmCanvasContext *ctx = wasm_canvas_graphics(env);
    SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
    char *text = ctx ? read_text(ctx, args[0].of.i32, args[1].of.i32) : NULL;
    if (text)
    {
        metrics = skia_measure_paragraph(text, args[2].of.f32, args[3].of.f32, args[4].of.f32,
                                         args[5].of.i32, NULL);
        free(text);
        uint8_t *out = args[6].of.i32 >= 0 ? wasm_canvas_guest_bytes(ctx, args[6].of.i32, 12) : NULL;
        if (out)
        {
            memcpy(out, &metrics.width, 4);
            memcpy(out + 4, &metrics.height, 4);
            memcpy(out + 8, &metrics.lines, 4);
        }
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = metrics.height;
    return NULL;
}

#define RICH_TEXT_MAX_SPANS 64

static int read_rich_text(WasmCanvasContext *ctx, int32_t ptr, int32_t count, SkiaTextSpan *spans)
{
    if (count < 0 || count > RICH_TEXT_MAX_SPANS)
        return -1;
    const uint8_t *records = count ? wasm_canvas_guest_bytes(ctx, ptr, count * 16) : NULL;
    if (count && !records)
        return -1;
    for (int i = 0; i < count; i++)
    {
        int32_t text_ptr, text_len;
        uint32_t color;
        float size;
        memcpy(&text_ptr, records + i * 16, 4);
        memcpy(&text_len, records + i * 16 + 4, 4);
        memcpy(&size, records + i * 16 + 8, 4);
        memcpy(&color, records + i * 16 + 12, 4);
        char *text = read_text(ctx, text_ptr, text_len);
        if (!text)
        {
            while (i-- > 0)
                free((char *)spans[i].text);
            return -1;
        }
        spans[i] = (SkiaTextSpan){text, size, NULL, color, color != 0};
    }
    return count;
}

static void free_rich_text(SkiaTextSpan *spans, int count)
{
    for (int i = 0; i < count; i++)
        free((char *)spans[i].text);
}

static wasm_trap_t *host_draw_rich_text(HOST_ARGUMENTS)
{
    WasmCanvasContext *ctx = wasm_canvas_graphics(env);
    SkiaCanvas *canvas = wasm_canvas_current_canvas(ctx);
    SkiaTextSpan spans[RICH_TEXT_MAX_SPANS];
    SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
    int count = canvas ? read_rich_text(ctx, args[0].of.i32, args[1].of.i32, spans) : -1;
    int align = args[5].of.i32;
    if (count >= 0)
    {
        metrics = skia_canvas_draw_rich_text(canvas, spans, count, args[2].of.f32, args[3].of.f32, args[4].of.f32,
                                             args[6].of.f32,
                                             align >= 0 && align <= 2 ? (SkiaTextAlign)align : SKIA_TEXT_ALIGN_LEFT,
                                             args[7].of.i32, wasm_canvas_active_paint(ctx));
        free_rich_text(spans, count);
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = metrics.height;
    return NULL;
}

static wasm_trap_t *host_measure_rich_text(HOST_ARGUMENTS)
{
    WasmCanvasContext *ctx = wasm_canvas_graphics(env);
    SkiaTextSpan spans[RICH_TEXT_MAX_SPANS];
    SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
    int count = ctx ? read_rich_text(ctx, args[0].of.i32, args[1].of.i32, spans) : -1;
    if (count >= 0)
    {
        metrics = skia_measure_rich_text(spans, count, args[2].of.f32, args[3].of.f32, args[4].of.i32);
        free_rich_text(spans, count);
        uint8_t *out = args[5].of.i32 >= 0 ? wasm_canvas_guest_bytes(ctx, args[5].of.i32, 12) : NULL;
        if (out)
        {
            memcpy(out, &metrics.width, 4);
            memcpy(out + 4, &metrics.height, 4);
            memcpy(out + 8, &metrics.lines, 4);
        }
    }
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = metrics.height;
    return NULL;
}

static wasm_trap_t *host_wait_for_input(HOST_ARGUMENTS)
{
    int width = 0, height = 0;
    BudoAnimationWait *wait = wasm_canvas_animation_wait(wasm_canvas_graphics(env), &width, &height);
    if (wait)
        budo_animation_wait_start(wait, args[0].of.f32, width, height);
    return NULL;
}

static wasmtime_error_t *define_effect_function(wasmtime_linker_t *linker, WasmCanvasContext *context,
                                                const char *name, wasmtime_func_callback_t callback,
                                                const wasm_valkind_t *param_types, size_t param_count,
                                                const wasm_valkind_t *result_types, size_t result_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t i = 0; i < param_count; i++)
        params.data[i] = wasm_valtype_new(param_types[i]);
    wasm_valtype_vec_new_uninitialized(&results, result_count);
    for (size_t i = 0; i < result_count; i++)
        results.data[i] = wasm_valtype_new(result_types[i]);
    wasm_functype_t *type = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(linker, "env", 3, name, strlen(name), type,
                                                          callback, context, NULL);
    wasm_functype_delete(type);
    return error;
}

static wasm_trap_t *host_path_add_svg(HOST_ARGUMENTS)
{
    WasmCanvasContext *ctx = wasm_canvas_graphics(env);
    SkiaPath *path = wasm_canvas_path(ctx, args[0].of.i32);
    int32_t length = args[2].of.i32;
    const uint8_t *bytes = length > 0 && length <= 65536 ? wasm_canvas_guest_bytes(ctx, args[1].of.i32, length) : NULL;
    char *data = bytes ? malloc((size_t)length + 1u) : NULL;
    bool added = false;
    if (path && data)
    {
        memcpy(data, bytes, (size_t)length);
        data[length] = '\0';
        added = skia_path_add_svg(path, data);
    }
    free(data);
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = added ? 1 : 0;
    return NULL;
}

wasmtime_error_t *wasm_canvas_effects_register(wasmtime_linker_t *linker, WasmCanvasContext *context)
{
    static const wasm_valkind_t linear[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_I32, WASM_I32, WASM_I32};
    static const wasm_valkind_t radial[] = {WASM_F32, WASM_F32, WASM_F32, WASM_I32, WASM_I32, WASM_I32};
    static const wasm_valkind_t sweep[] = {WASM_F32, WASM_F32, WASM_I32, WASM_I32, WASM_I32};
    static const wasm_valkind_t path[] = {WASM_I32};
    static const wasm_valkind_t path_svg[] = {WASM_I32, WASM_I32, WASM_I32};
    static const wasm_valkind_t added[] = {WASM_I32};
    static const wasm_valkind_t draw[] = {WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32,
                                          WASM_I32, WASM_F32, WASM_I32};
    static const wasm_valkind_t measure[] = {WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_I32, WASM_I32};
    static const wasm_valkind_t height[] = {WASM_F32};
    static const wasm_valkind_t timeout[] = {WASM_F32};
    static const wasm_valkind_t rich_draw[] = {WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_I32, WASM_F32, WASM_I32};
    static const wasm_valkind_t rich_measure[] = {WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_I32, WASM_I32};
    wasmtime_error_t *error;

    if ((error = define_effect_function(linker, context, "canvas_set_linear_gradient", host_set_linear_gradient, linear, 7, NULL, 0)) ||
        (error = define_effect_function(linker, context, "canvas_set_radial_gradient", host_set_radial_gradient, radial, 6, NULL, 0)) ||
        (error = define_effect_function(linker, context, "canvas_set_sweep_gradient", host_set_sweep_gradient, sweep, 5, NULL, 0)) ||
        (error = define_effect_function(linker, context, "canvas_clear_gradient", host_clear_gradient, NULL, 0, NULL, 0)) ||
        (error = define_effect_function(linker, context, "canvas_clip_path", host_clip_path, path, 1, NULL, 0)) ||
        (error = define_effect_function(linker, context, "path_add_svg", host_path_add_svg, path_svg, 3, added, 1)) ||
        (error = define_effect_function(linker, context, "animation_wait_for_input", host_wait_for_input, timeout, 1, NULL, 0)) ||
        (error = define_effect_function(linker, context, "canvas_draw_rich_text", host_draw_rich_text, rich_draw, 8, height, 1)) ||
        (error = define_effect_function(linker, context, "canvas_measure_rich_text", host_measure_rich_text, rich_measure, 6, height, 1)) ||
        (error = define_effect_function(linker, context, "canvas_draw_paragraph", host_draw_paragraph, draw, 9, height, 1)) ||
        (error = define_effect_function(linker, context, "canvas_measure_paragraph", host_measure_paragraph, measure, 7, height, 1)))
        return error;
    return NULL;
}