#include "graphics/js_canvas_effects_bindings.h"
#include "graphics/color_util.h"

#include <string.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#endif

static uint32_t js_get_color(JSContext *ctx, JSValueConst value)
{
    uint32_t color = SKIA_COLOR_BLACK;
    if (JS_IsNumber(value))
    {
        int64_t number = 0;
        JS_ToInt64(ctx, &number, value);
        return (uint32_t)number;
    }
    const char *text = JS_IsString(value) ? JS_ToCString(ctx, value) : NULL;
    if (text)
    {
        color_parse_hex_string(text, &color);
        JS_FreeCString(ctx, text);
    }
    return color;
}

static bool js_is_missing(JSValueConst value)
{
    return JS_IsUndefined(value) || JS_IsNull(value);
}

static int js_read_gradient(JSContext *ctx, JSValueConst colors_value, JSValueConst stops_value,
                            uint32_t *colors, float *stops, bool *has_stops)
{
    uint32_t count = 0;
    JSValue length = JS_GetPropertyStr(ctx, colors_value, "length");
    int failed = !BUDO_JS_IS_ARRAY(ctx, colors_value) || JS_ToUint32(ctx, &count, length) < 0;
    JS_FreeValue(ctx, length);
    if (failed || count < 2 || count > SKIA_GRADIENT_MAX_STOPS)
    {
        JS_ThrowTypeError(ctx, "sys.canvas.setGradient: colors must be an array of 2 to %d colors",
                          SKIA_GRADIENT_MAX_STOPS);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        JSValue item = JS_GetPropertyUint32(ctx, colors_value, i);
        colors[i] = js_get_color(ctx, item);
        JS_FreeValue(ctx, item);
    }
    *has_stops = !js_is_missing(stops_value);
    if (!*has_stops)
        return (int)count;
    uint32_t stop_count = 0;
    length = JS_GetPropertyStr(ctx, stops_value, "length");
    failed = !BUDO_JS_IS_ARRAY(ctx, stops_value) || JS_ToUint32(ctx, &stop_count, length) < 0;
    JS_FreeValue(ctx, length);
    if (failed || stop_count != count)
    {
        JS_ThrowTypeError(ctx, "sys.canvas.setGradient: stops must be an array with one number per color");
        return -1;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        double stop = 0.0;
        JSValue item = JS_GetPropertyUint32(ctx, stops_value, i);
        int rc = JS_ToFloat64(ctx, &stop, item);
        JS_FreeValue(ctx, item);
        if (rc < 0)
            return -1;
        stops[i] = (float)stop;
    }
    return (int)count;
}

static JSValue op_set_gradient(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint)
        return JS_ThrowInternalError(ctx, "sys.canvas.setGradient: no active paint");
    if (argc < 1 || js_is_missing(argv[0]))
    {
        skia_paint_clear_shader(drawingDesk->active_paint);
        return JS_TRUE;
    }
    const char *kind = JS_ToCString(ctx, argv[0]);
    if (!kind)
        return JS_EXCEPTION;
    int numbers = strcmp(kind, "linear") == 0 ? 4 : strcmp(kind, "radial") == 0 ? 3
                                               : strcmp(kind, "sweep") == 0    ? 2
                                               : strcmp(kind, "none") == 0     ? 0
                                                                               : -1;
    if (numbers <= 0)
    {
        JSValue result = numbers == 0 ? JS_TRUE
                                      : JS_ThrowRangeError(ctx, "sys.canvas.setGradient: unknown gradient '%s'", kind);
        if (numbers == 0)
            skia_paint_clear_shader(drawingDesk->active_paint);
        JS_FreeCString(ctx, kind);
        return result;
    }
    double values[4] = {0.0, 0.0, 0.0, 0.0};
    if (argc < numbers + 2)
    {
        JS_FreeCString(ctx, kind);
        return JS_ThrowTypeError(ctx, "sys.canvas.setGradient: too few arguments");
    }
    for (int i = 0; i < numbers; i++)
    {
        if (!JS_IsNumber(argv[1 + i]) || JS_ToFloat64(ctx, &values[i], argv[1 + i]) < 0)
        {
            JS_FreeCString(ctx, kind);
            return JS_ThrowTypeError(ctx, "sys.canvas.setGradient: coordinates must be numbers");
        }
    }
    uint32_t colors[SKIA_GRADIENT_MAX_STOPS];
    float stops[SKIA_GRADIENT_MAX_STOPS];
    bool has_stops = false;
    int count = js_read_gradient(ctx, argv[1 + numbers], argc > numbers + 2 ? argv[2 + numbers] : JS_UNDEFINED,
                                 colors, stops, &has_stops);
    if (count < 0)
    {
        JS_FreeCString(ctx, kind);
        return JS_EXCEPTION;
    }
    bool ok;
    const float *stop_values = has_stops ? stops : NULL;
    if (numbers == 4)
        ok = skia_paint_set_linear_gradient(drawingDesk->active_paint, values[0], values[1], values[2], values[3],
                                            colors, stop_values, count);
    else if (numbers == 3)
        ok = skia_paint_set_radial_gradient(drawingDesk->active_paint, values[0], values[1], values[2],
                                            colors, stop_values, count);
    else
        ok = skia_paint_set_sweep_gradient(drawingDesk->active_paint, values[0], values[1],
                                           colors, stop_values, count);
    JS_FreeCString(ctx, kind);
    return JS_NewBool(ctx, ok);
}

static int js_read_paragraph_options(JSContext *ctx, JSValueConst options, SkiaTextAlign *align,
                                     double *line_height, int *max_lines)
{
    *align = SKIA_TEXT_ALIGN_LEFT;
    *line_height = SKIA_DEFAULT_LINE_HEIGHT;
    *max_lines = 0;
    if (js_is_missing(options))
        return 0;
    if (!JS_IsObject(options))
    {
        JS_ThrowTypeError(ctx, "sys.canvas paragraph options must be an object");
        return -1;
    }
    JSValue value = JS_GetPropertyStr(ctx, options, "align");
    if (!js_is_missing(value))
    {
        const char *name = JS_ToCString(ctx, value);
        int known = 1;
        if (name && strcmp(name, "center") == 0)
            *align = SKIA_TEXT_ALIGN_CENTER;
        else if (name && strcmp(name, "right") == 0)
            *align = SKIA_TEXT_ALIGN_RIGHT;
        else if (!name || strcmp(name, "left") != 0)
            known = 0;
        if (!known)
            JS_ThrowRangeError(ctx, "sys.canvas paragraph align must be 'left', 'center' or 'right'");
        JS_FreeCString(ctx, name);
        if (!known)
        {
            JS_FreeValue(ctx, value);
            return -1;
        }
    }
    JS_FreeValue(ctx, value);
    value = JS_GetPropertyStr(ctx, options, "lineHeight");
    int rc = js_is_missing(value) ? 0 : JS_ToFloat64(ctx, line_height, value);
    JS_FreeValue(ctx, value);
    if (rc < 0)
        return -1;
    value = JS_GetPropertyStr(ctx, options, "maxLines");
    rc = js_is_missing(value) ? 0 : JS_ToInt32(ctx, max_lines, value);
    JS_FreeValue(ctx, value);
    return rc;
}

static JSValue js_paragraph_metrics(JSContext *ctx, SkiaParagraphMetrics metrics)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, metrics.width));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewFloat64(ctx, metrics.height));
    JS_SetPropertyStr(ctx, obj, "lines", JS_NewInt32(ctx, metrics.lines));
    return obj;
}

static JSValue op_canvas_paragraph(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, SkiaFont *font,
                                   bool draw, int argc, JSValue *argv)
{
    int first = draw ? 3 : 1;
    if (argc < first + 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, draw ? "sys.canvas.drawParagraph(text, x, y, width, fontSize?, options?)"
                                           : "sys.canvas.measureParagraph(text, width, fontSize?, options?)");
    double x = 0.0, y = 0.0, width = 0.0, font_size = BUDO_DEFAULT_FONT_SIZE, line_height;
    if (draw && (JS_ToFloat64(ctx, &x, argv[1]) < 0 || JS_ToFloat64(ctx, &y, argv[2]) < 0))
        return JS_EXCEPTION;
    if (!js_is_missing(argv[first]) && JS_ToFloat64(ctx, &width, argv[first]) < 0)
        return JS_EXCEPTION;
    if (argc > first + 1 && !js_is_missing(argv[first + 1]) && JS_ToFloat64(ctx, &font_size, argv[first + 1]) < 0)
        return JS_EXCEPTION;
    SkiaTextAlign align;
    int max_lines;
    if (js_read_paragraph_options(ctx, argc > first + 2 ? argv[first + 2] : JS_UNDEFINED,
                                  &align, &line_height, &max_lines) < 0)
        return JS_EXCEPTION;
    const char *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    SkiaParagraphMetrics metrics =
        draw ? skia_canvas_draw_paragraph(canvas, text, x, y, width, font_size, line_height, align,
                                          max_lines, paint, font)
             : skia_measure_paragraph(text, width, font_size, line_height, max_lines, font);
    JS_FreeCString(ctx, text);
    return js_paragraph_metrics(ctx, metrics);
}

static JSValue js_set_gradient(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    return op_set_gradient(ctx, &graphic_ctx->drawingDesk, argc, argv);
}

static JSValue js_canvas_draw_paragraph(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    return op_canvas_paragraph(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint,
                               graphic_ctx->drawingDesk.active_font, true, argc, argv);
}

static JSValue js_canvas_measure_paragraph(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    return op_canvas_paragraph(ctx, NULL, NULL, graphic_ctx->drawingDesk.active_font, false, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_gradient(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    SkiaDrawingDesk *drawingDesk = js_canvas_texture_drawing_desk(ctx, graphic_ctx, this_val);
    if (!drawingDesk)
        return JS_EXCEPTION;
    return op_set_gradient(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_paragraph(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    SkiaDrawingDesk *drawingDesk = js_canvas_texture_drawing_desk(ctx, graphic_ctx, this_val);
    if (!drawingDesk)
        return JS_EXCEPTION;
    return op_canvas_paragraph(ctx, drawingDesk->canvas, drawingDesk->active_paint, drawingDesk->active_font,
                               true, argc, argv);
}

static JSValue js_canvas_texture_canvas_measure_paragraph(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    SkiaDrawingDesk *drawingDesk = js_canvas_texture_drawing_desk(ctx, graphic_ctx, this_val);
    if (!drawingDesk)
        return JS_EXCEPTION;
    return op_canvas_paragraph(ctx, NULL, NULL, drawingDesk->active_font, false, argc, argv);
}

static JSValue transform_clip_round_rect(JSContext *ctx, JSValueConst this_value,
                                         int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    double values[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < 5; i++)
        if (i < argument_count && JS_ToFloat64(ctx, &values[i], arguments[i]) < 0)
            return JS_EXCEPTION;
    values[5] = values[4];
    if (argument_count > 5 && !js_is_missing(arguments[5]) && JS_ToFloat64(ctx, &values[5], arguments[5]) < 0)
        return JS_EXCEPTION;
    skia_canvas_clip_round_rect(graphic_ctx->drawingDesk.canvas, values[0], values[1],
                                values[0] + values[2], values[1] + values[3], values[4], values[5]);
    return JS_UNDEFINED;
}

static JSValue transform_clip_path(JSContext *ctx, JSValueConst this_value,
                                   int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    int id = -1;
    if (argument_count < 1 || JS_ToInt32(ctx, &id, arguments[0]) < 0)
        return JS_EXCEPTION;
    SkiaPath *path = id >= 0 && id < graphic_ctx->path_count ? graphic_ctx->paths[id] : NULL;
    if (!path)
        return JS_ThrowRangeError(ctx, "sys.canvas.clipPath: unknown path %d", id);
    skia_canvas_clip_path(graphic_ctx->drawingDesk.canvas, path);
    return JS_UNDEFINED;
}

static JSValue transform_save_layer(JSContext *ctx, JSValueConst this_value,
                                    int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    double alpha = 255.0, backdrop = 0.0, bounds[4] = {0.0, 0.0, 0.0, 0.0};
    if (argument_count > 0 && !js_is_missing(arguments[0]) && JS_ToFloat64(ctx, &alpha, arguments[0]) < 0)
        return JS_EXCEPTION;
    bool has_bounds = argument_count > 4 && !js_is_missing(arguments[1]);
    for (int i = 0; has_bounds && i < 4; i++)
        if (JS_ToFloat64(ctx, &bounds[i], arguments[1 + i]) < 0)
            return JS_EXCEPTION;
    if (argument_count > 5 && !js_is_missing(arguments[5]) && JS_ToFloat64(ctx, &backdrop, arguments[5]) < 0)
        return JS_EXCEPTION;
    alpha = alpha < 0.0 ? 0.0 : alpha > 255.0 ? 255.0 : alpha;
    SkiaRect rect = {(float)bounds[0], (float)bounds[1], (float)(bounds[0] + bounds[2]), (float)(bounds[1] + bounds[3])};
    skia_canvas_save_layer(graphic_ctx->drawingDesk.canvas, has_bounds ? &rect : NULL,
                           (uint8_t)(alpha + 0.5), (float)backdrop);
    return JS_UNDEFINED;
}

#define RICH_TEXT_MAX_SPANS 64

static int rich_text_read(JSContext *ctx, JSGraphicContext *graphic_ctx, JSValueConst value, double default_size,
                          SkiaFont *default_font, SkiaTextSpan *spans)
{
    uint32_t count = 0;
    JSValue length = JS_GetPropertyStr(ctx, value, "length");
    int failed = !BUDO_JS_IS_ARRAY(ctx, value) || JS_ToUint32(ctx, &count, length) < 0;
    JS_FreeValue(ctx, length);
    if (failed || count > RICH_TEXT_MAX_SPANS)
    {
        JS_ThrowTypeError(ctx, "sys.canvas rich text: spans must be an array of at most %d strings or "
                               "{ text, size?, color?, font? } objects", RICH_TEXT_MAX_SPANS);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        JSValue item = JS_GetPropertyUint32(ctx, value, i);
        SkiaTextSpan span = {NULL, (float)default_size, default_font, 0, false};
        JSValue text = JS_IsString(item) ? JS_DupValue(ctx, item) : JS_GetPropertyStr(ctx, item, "text");
        int ok = JS_IsString(text);
        if (ok && JS_IsObject(item))
        {
            JSValue size = JS_GetPropertyStr(ctx, item, "size");
            JSValue color = JS_GetPropertyStr(ctx, item, "color");
            JSValue font = JS_GetPropertyStr(ctx, item, "font");
            double number = default_size;
            if (!js_is_missing(size) && JS_ToFloat64(ctx, &number, size) == 0)
                span.size = (float)number;
            if (!js_is_missing(color))
            {
                span.color = js_get_color(ctx, color);
                span.has_color = true;
            }
            if (!js_is_missing(font))
            {
                const char *name = JS_ToCString(ctx, font);
                span.font = NULL;
                for (int f = 0; name && f < graphic_ctx->font_count; f++)
                    if (graphic_ctx->font_names[f] && strcmp(graphic_ctx->font_names[f], name) == 0)
                        span.font = graphic_ctx->fonts[f];
                if (!span.font)
                    ok = 0, JS_ThrowReferenceError(ctx, "sys.canvas rich text: unknown font '%s'", name ? name : "");
                JS_FreeCString(ctx, name);
            }
            JS_FreeValue(ctx, size);
            JS_FreeValue(ctx, color);
            JS_FreeValue(ctx, font);
        }
        else if (!ok)
            JS_ThrowTypeError(ctx, "sys.canvas rich text: span %u has no text", (unsigned)i);
        if (ok)
            span.text = JS_ToCString(ctx, text);
        JS_FreeValue(ctx, text);
        JS_FreeValue(ctx, item);
        if (!ok || !span.text)
        {
            for (uint32_t k = 0; k < i; k++)
                JS_FreeCString(ctx, spans[k].text);
            return -1;
        }
        spans[i] = span;
    }
    return (int)count;
}

static JSValue op_canvas_rich_text(JSContext *ctx, JSGraphicContext *graphic_ctx, SkiaCanvas *canvas,
                                   SkiaPaint *paint, SkiaFont *font, bool draw, int argc, JSValue *argv)
{
    int first = draw ? 3 : 1;
    if (argc < first + 1)
        return JS_ThrowTypeError(ctx, draw ? "sys.canvas.drawRichText(spans, x, y, width, options?)"
                                           : "sys.canvas.measureRichText(spans, width, options?)");
    double x = 0.0, y = 0.0, width = 0.0, line_height, size = BUDO_DEFAULT_FONT_SIZE;
    if (draw && (JS_ToFloat64(ctx, &x, argv[1]) < 0 || JS_ToFloat64(ctx, &y, argv[2]) < 0))
        return JS_EXCEPTION;
    if (!js_is_missing(argv[first]) && JS_ToFloat64(ctx, &width, argv[first]) < 0)
        return JS_EXCEPTION;
    JSValueConst options = argc > first + 1 ? argv[first + 1] : JS_UNDEFINED;
    SkiaTextAlign align;
    int max_lines;
    if (js_read_paragraph_options(ctx, options, &align, &line_height, &max_lines) < 0)
        return JS_EXCEPTION;
    if (JS_IsObject(options))
    {
        JSValue value = JS_GetPropertyStr(ctx, options, "size");
        int rc = js_is_missing(value) ? 0 : JS_ToFloat64(ctx, &size, value);
        JS_FreeValue(ctx, value);
        if (rc < 0)
            return JS_EXCEPTION;
    }
    SkiaTextSpan spans[RICH_TEXT_MAX_SPANS];
    int count = rich_text_read(ctx, graphic_ctx, argv[0], size, font, spans);
    if (count < 0)
        return JS_EXCEPTION;
    SkiaParagraphMetrics metrics =
        draw ? skia_canvas_draw_rich_text(canvas, spans, count, x, y, width, line_height, align, max_lines, paint)
             : skia_measure_rich_text(spans, count, width, line_height, max_lines);
    for (int i = 0; i < count; i++)
        JS_FreeCString(ctx, spans[i].text);
    return js_paragraph_metrics(ctx, metrics);
}

static JSValue js_canvas_draw_rich_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    return op_canvas_rich_text(ctx, graphic_ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint,
                               graphic_ctx->drawingDesk.active_font, true, argc, argv);
}

static JSValue js_canvas_measure_rich_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    return op_canvas_rich_text(ctx, graphic_ctx, NULL, NULL, graphic_ctx->drawingDesk.active_font, false, argc, argv);
}

static JSValue js_path_add_svg(JSContext *ctx, JSValueConst this_value,
                               int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    int id = -1;
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argument_count < 2 || JS_ToInt32(ctx, &id, arguments[0]) < 0)
        return JS_EXCEPTION;
    SkiaPath *path = id >= 0 && id < graphic_ctx->path_count ? graphic_ctx->paths[id] : NULL;
    if (!path)
        return JS_FALSE;
    const char *data = JS_ToCString(ctx, arguments[1]);
    if (!data)
        return JS_EXCEPTION;
    bool added = skia_path_add_svg(path, data);
    JS_FreeCString(ctx, data);
    return JS_NewBool(ctx, added);
}

static const JsGraphicFunction js_path_effect_funcs[] = {
    {"addSvg", 2, js_path_add_svg},
};

static const JsGraphicFunction js_canvas_effect_funcs[] = {
    {"clipRoundRect", 6, transform_clip_round_rect},
    {"clipPath", 1, transform_clip_path},
    {"saveLayer", 6, transform_save_layer},
    {"setGradient", 7, js_set_gradient},
    {"drawParagraph", 6, js_canvas_draw_paragraph},
    {"measureParagraph", 4, js_canvas_measure_paragraph},
    {"drawRichText", 5, js_canvas_draw_rich_text},
    {"measureRichText", 3, js_canvas_measure_rich_text},
};

static const JsGraphicFunction js_canvas_texture_effect_funcs[] = {
    {"setGradient", 7, js_canvas_texture_canvas_set_gradient},
    {"drawParagraph", 6, js_canvas_texture_canvas_draw_paragraph},
    {"measureParagraph", 4, js_canvas_texture_canvas_measure_paragraph},
};

void js_canvas_effects_register(JSContext *ctx, JSValueConst sys_obj, JSGraphicContext *graphic_ctx)
{
    JSValue canvas = JS_GetPropertyStr(ctx, sys_obj, "canvas");
    for (size_t i = 0; i < sizeof(js_canvas_effect_funcs) / sizeof(js_canvas_effect_funcs[0]); i++)
        js_graphic_register_function(ctx, canvas, &js_canvas_effect_funcs[i], graphic_ctx);
    JS_FreeValue(ctx, canvas);
    JSValue path = JS_GetPropertyStr(ctx, sys_obj, "path");
    for (size_t i = 0; i < sizeof(js_path_effect_funcs) / sizeof(js_path_effect_funcs[0]); i++)
        js_graphic_register_function(ctx, path, &js_path_effect_funcs[i], graphic_ctx);
    JS_FreeValue(ctx, path);
}

void js_canvas_texture_effects_register(JSContext *ctx, JSValueConst canvas_obj, JSGraphicContext *graphic_ctx)
{
    for (size_t i = 0; i < sizeof(js_canvas_texture_effect_funcs) / sizeof(js_canvas_texture_effect_funcs[0]); i++)
        js_graphic_register_function(ctx, canvas_obj, &js_canvas_texture_effect_funcs[i], graphic_ctx);
}