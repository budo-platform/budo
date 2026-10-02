#include "graphics/js_canvas_bindings.h"
#include "graphics/js_gl_bindings.h"
#include "graphics/js_tools.h"
#include "graphics/color_util.h"
#include "core/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#endif

#ifdef __ANDROID__
#include <android/log.h>
#define JS_LOG_TAG "BudoJS"
#endif

int js_graphic_register_function(JSContext *ctx, JSValue obj,
                                 const JsGraphicFunction *definition,
                                 JSGraphicContext *graphic_ctx)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&graphic_ctx, sizeof(graphic_ctx));
    if (JS_IsException(data))
        return -1;

    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;

    return JS_SetPropertyStr(ctx, obj, definition->name, function);
}

JSGraphicContext *js_graphic_context(JSContext *ctx, JSValueConst *func_data)
{
    JSGraphicContext *graphic_ctx = NULL;

    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);

    if (data && size == sizeof(graphic_ctx))
        memcpy(&graphic_ctx, data, size);

    if (graphic_ctx)
        budo_graphics_activation_request(&graphic_ctx->activation);

    return graphic_ctx;
}

static const JsGraphicFunction js_canvas_texture_funcs[2];

typedef struct
{
    const char *name;
    SkiaBlendMode mode;
} BlendModeEntry;

static const BlendModeEntry k_blend_mode_table[24];

static const uint8_t *js_get_buffer_bytes(JSContext *ctx, JSValue val,
                                          size_t *out_byte_count,
                                          JSValue *out_ab_ref)
{
    *out_ab_ref = JS_UNDEFINED;
    *out_byte_count = 0;

    {
        size_t byte_offset = 0, byte_length = 0, bpe = 0;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bpe);
        if (!JS_IsException(ab))
        {
            size_t ab_size = 0;
            uint8_t *raw = JS_GetArrayBuffer(ctx, &ab_size, ab);
            if (raw)
            {
                *out_byte_count = byte_length;
                *out_ab_ref = ab;
                return raw + byte_offset;
            }
            JS_FreeValue(ctx, ab);
        }
    }

    {
        size_t ab_size = 0;
        uint8_t *raw = JS_GetArrayBuffer(ctx, &ab_size, val);
        if (raw)
        {
            *out_byte_count = ab_size;
            return raw;
        }
    }

    return NULL;
}

static int js_find_font_index(JSGraphicContext *graphic_ctx, const char *name)
{
    if (!graphic_ctx || !name)
    {
        return -1;
    }

    for (int i = 0; i < graphic_ctx->font_count; i++)
    {
        if (graphic_ctx->font_names[i] && strcmp(graphic_ctx->font_names[i], name) == 0)
        {
            return i;
        }
    }

    return -1;
}

static bool js_ensure_font_capacity(JSGraphicContext *graphic_ctx)
{
    SkiaFont **fonts;
    char **font_names;
    int new_capacity;

    if (!graphic_ctx)
    {
        return false;
    }

    if (graphic_ctx->font_count < graphic_ctx->font_capacity)
    {
        return true;
    }

    new_capacity = graphic_ctx->font_capacity > 0 ? graphic_ctx->font_capacity * 2 : 4;
    fonts = (SkiaFont **)calloc((size_t)new_capacity, sizeof(SkiaFont *));
    font_names = (char **)calloc((size_t)new_capacity, sizeof(char *));
    if (!fonts || !font_names)
    {
        free(fonts);
        free(font_names);
        return false;
    }

    for (int i = 0; i < graphic_ctx->font_count; i++)
    {
        fonts[i] = graphic_ctx->fonts[i];
        font_names[i] = graphic_ctx->font_names[i];
    }

    free(graphic_ctx->fonts);
    free(graphic_ctx->font_names);
    graphic_ctx->fonts = fonts;
    graphic_ctx->font_names = font_names;
    graphic_ctx->font_capacity = new_capacity;
    return true;
}

static bool js_ensure_canvas_texture_capacity(JSGraphicContext *graphic_ctx)
{
    CanvasTexture **items;
    int new_capacity;

    if (!graphic_ctx)
        return false;
    if (graphic_ctx->canvas_texture_count < graphic_ctx->canvas_texture_capacity)
        return true;

    new_capacity = graphic_ctx->canvas_texture_capacity > 0 ? graphic_ctx->canvas_texture_capacity * 2 : 4;
    items = (CanvasTexture **)calloc((size_t)new_capacity, sizeof(CanvasTexture *));
    if (!items)
        return false;

    for (int i = 0; i < graphic_ctx->canvas_texture_capacity; i++)
        items[i] = graphic_ctx->canvas_textures[i];

    free(graphic_ctx->canvas_textures);
    graphic_ctx->canvas_textures = items;
    graphic_ctx->canvas_texture_capacity = new_capacity;
    return true;
}

static int js_register_canvas_texture(JSGraphicContext *graphic_ctx, CanvasTexture *canvas_texture)
{
    int i;

    if (!graphic_ctx || !canvas_texture)
        return -1;

    for (i = 0; i < graphic_ctx->canvas_texture_capacity; i++)
    {
        if (!graphic_ctx->canvas_textures[i])
        {
            graphic_ctx->canvas_textures[i] = canvas_texture;
            if (i >= graphic_ctx->canvas_texture_count)
                graphic_ctx->canvas_texture_count = i + 1;
            return i + 1;
        }
    }

    if (!js_ensure_canvas_texture_capacity(graphic_ctx))
        return -1;

    graphic_ctx->canvas_textures[graphic_ctx->canvas_texture_count] = canvas_texture;
    graphic_ctx->canvas_texture_count += 1;
    return graphic_ctx->canvas_texture_count;
}

static CanvasTexture *js_get_canvas_texture_by_id(JSGraphicContext *graphic_ctx, int id)
{
    if (!graphic_ctx || id <= 0 || id > graphic_ctx->canvas_texture_count)
        return NULL;
    return graphic_ctx->canvas_textures[id - 1];
}

static int js_get_canvas_texture_id_from_value(JSContext *ctx, JSValue value)
{
    int id = 0;

    if (JS_IsNumber(value))
    {
        JS_ToInt32(ctx, &id, value);
        return id;
    }
    if (JS_IsObject(value))
    {
        JSValue prop = JS_GetPropertyStr(ctx, value, "__canvasTextureId");
        if (!JS_IsUndefined(prop))
            JS_ToInt32(ctx, &id, prop);
        JS_FreeValue(ctx, prop);
    }
    return id;
}

static CanvasTexture *js_get_canvas_texture_from_value(JSContext *ctx, JSGraphicContext *graphic_ctx, JSValue value)
{
    int id = js_get_canvas_texture_id_from_value(ctx, value);
    return js_get_canvas_texture_by_id(graphic_ctx, id);
}

static CanvasTexture *js_get_canvas_texture_from_this(JSContext *ctx, JSGraphicContext *graphic_ctx, JSValue this_val)
{
    CanvasTexture *canvas_texture = js_get_canvas_texture_from_value(ctx, graphic_ctx, this_val);
    if (!canvas_texture)
        JS_ThrowReferenceError(ctx, "Invalid or destroyed CanvasTexture");
    return canvas_texture;
}

static void js_canvas_texture_set_id(JSContext *ctx, JSValue obj, int id)
{
    JS_SetPropertyStr(ctx, obj, "__canvasTextureId", JS_NewInt32(ctx, id));
}

static void js_canvas_texture_update_properties(JSContext *ctx, JSValue obj, CanvasTexture *canvas_texture)
{
    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, window_canvas_texture_get_width(canvas_texture)));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, window_canvas_texture_get_height(canvas_texture)));
    JS_SetPropertyStr(ctx, obj, "texture", JS_NewUint32(ctx, window_canvas_texture_get_gl_texture(canvas_texture)));
    JS_SetPropertyStr(ctx, obj, "target", JS_NewUint32(ctx, window_canvas_texture_get_gl_framebuffer(canvas_texture)));
}

static uint32_t js_get_color(JSContext *ctx, JSValue val)
{
    if (JS_IsNumber(val))
    {
        int64_t color;
        JS_ToInt64(ctx, &color, val);
        return (uint32_t)color;
    }
    if (JS_IsString(val))
    {
        const char *str = JS_ToCString(ctx, val);
        if (str)
        {
            uint32_t color = 0xFF000000; 
            color_parse_hex_string(str, &color);
            JS_FreeCString(ctx, str);
            return color;
        }
    }
    return SKIA_COLOR_BLACK;
}

static JSValue op_canvas_clear(JSContext *ctx, SkiaCanvas *canvas, int argc, JSValue *argv)
{
    if (!canvas)
        return JS_UNDEFINED;

    uint32_t color = SKIA_COLOR_WHITE;
    if (argc >= 1)
    {
        color = js_get_color(ctx, argv[0]);
    }

    skia_canvas_clear(canvas, color);
    return JS_UNDEFINED;
}

static JSValue op_canvas_draw_rect(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 4)
        return JS_UNDEFINED;

    double x, y, w, h;
    JS_ToFloat64(ctx, &x, argv[0]);
    JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]);
    JS_ToFloat64(ctx, &h, argv[3]);

    skia_canvas_draw_rect(canvas, x, y, x + w, y + h, paint);

    return JS_UNDEFINED;
}

static JSValue op_canvas_draw_circle(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 3)
        return JS_UNDEFINED;

    double cx, cy, radius;
    JS_ToFloat64(ctx, &cx, argv[0]);
    JS_ToFloat64(ctx, &cy, argv[1]);
    JS_ToFloat64(ctx, &radius, argv[2]);

    skia_canvas_draw_circle(canvas, cx, cy, radius, paint);
    return JS_UNDEFINED;
}

static JSValue op_canvas_draw_text(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, SkiaFont *font, int argc, JSValue *argv)
{
    if (!canvas || argc < 3)
        return JS_UNDEFINED;

    const char *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_UNDEFINED;

    double x, y, font_size = BUDO_DEFAULT_FONT_SIZE;
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);
    if (argc >= 4)
    {
        JS_ToFloat64(ctx, &font_size, argv[3]);
    }

    float width = skia_canvas_draw_text_with_font(canvas, text, x, y, font_size, paint, font);
    JS_FreeCString(ctx, text);
    return JS_NewFloat64(ctx, (double)width);
}

static JSValue op_canvas_read_pixels(JSContext *ctx, SkiaCanvas *canvas, int argc, JSValue *argv)
{
    if (!canvas)
        return JS_ThrowInternalError(ctx, "No canvas");

    int w = 0, h = 0;
    if (!skia_canvas_get_size(canvas, &w, &h) || w <= 0 || h <= 0)
        return JS_ThrowInternalError(ctx, "Canvas has no readable size");

    size_t bytes = (size_t)w * (size_t)h * 4u;
    uint8_t *pixels = (uint8_t *)malloc(bytes);
    if (!pixels)
        return JS_ThrowOutOfMemory(ctx);

    if (!skia_canvas_read_pixels(canvas, pixels))
    {
        free(pixels);
        return JS_ThrowInternalError(ctx, "skia_canvas_read_pixels failed");
    }

    JSValue ab = JS_NewArrayBufferCopy(ctx, pixels, bytes);
    free(pixels);
    if (JS_IsException(ab))
        return ab;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, h));
    JS_SetPropertyStr(ctx, obj, "pixels", ab);
    return obj;
}

static JSValue op_canvas_draw_round_rect(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 3)
        return JS_UNDEFINED;

    double x, y, w, h, rx, ry;
    JS_ToFloat64(ctx, &x, argv[0]);
    JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]);
    JS_ToFloat64(ctx, &h, argv[3]);
    JS_ToFloat64(ctx, &rx, argv[4]);
    JS_ToFloat64(ctx, &ry, argv[5]);

    skia_canvas_draw_round_rect(canvas, x, y, x + w, y + h, rx, ry, paint);
    return JS_UNDEFINED;
}

static JSValue op_canvas_draw_oval(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 3)
        return JS_UNDEFINED;

    double x, y, w, h;
    JS_ToFloat64(ctx, &x, argv[0]);
    JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]);
    JS_ToFloat64(ctx, &h, argv[3]);

    skia_canvas_draw_oval(canvas, x, y, x + w, y + h, paint);
    return JS_UNDEFINED;
}

static JSValue op_canvas_draw_line(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 4)
        return JS_UNDEFINED;

    double x1, y1, x2, y2;
    JS_ToFloat64(ctx, &x1, argv[0]);
    JS_ToFloat64(ctx, &y1, argv[1]);
    JS_ToFloat64(ctx, &x2, argv[2]);
    JS_ToFloat64(ctx, &y2, argv[3]);

    skia_canvas_draw_line(canvas, x1, y1, x2, y2, paint);
    return JS_UNDEFINED;
}

static JSValue op_canvas_draw_point(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 2)
        return JS_UNDEFINED;

    double x, y;
    JS_ToFloat64(ctx, &x, argv[0]);
    JS_ToFloat64(ctx, &y, argv[1]);

    skia_canvas_draw_point(canvas, x, y, paint);
    return JS_UNDEFINED;
}

static JSValue op_canvas_measure_text(JSContext *ctx, SkiaFont *font, int argc, JSValue *argv)
{
    if (argc < 1)
        return JS_NewFloat64(ctx, 0.0);

    const char *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_NewFloat64(ctx, 0.0);

    double font_size = BUDO_DEFAULT_FONT_SIZE;
    if (argc >= 2)
        JS_ToFloat64(ctx, &font_size, argv[1]);

    float width = skia_measure_text_width(text, font_size, font);
    JS_FreeCString(ctx, text);
    return JS_NewFloat64(ctx, (double)width);
}

static JSValue op_canvas_measure_text_rect(JSContext *ctx, SkiaFont *font, int argc, JSValue *argv)
{
    if (argc < 1)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewFloat64(ctx, 0.0));
        return obj;
    }

    const char *text = JS_ToCString(ctx, argv[0]);
    if (!text)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewFloat64(ctx, 0.0));
        return obj;
    }

    double font_size = BUDO_DEFAULT_FONT_SIZE;
    if (argc >= 2)
        JS_ToFloat64(ctx, &font_size, argv[1]);

    float w = 0.0f, h = 0.0f;
    skia_measure_text_rect(text, font_size, font, &w, &h);
    JS_FreeCString(ctx, text);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, (double)w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewFloat64(ctx, (double)h));
    return obj;
}

static JSValue op_canvas_draw_arc(JSContext *ctx, SkiaCanvas *canvas, SkiaPaint *paint, int argc, JSValue *argv)
{
    if (!canvas || !paint || argc < 6)
        return JS_UNDEFINED;

    double x, y, w, h, start_angle, sweep_angle;
    JS_ToFloat64(ctx, &x, argv[0]);
    JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]);
    JS_ToFloat64(ctx, &h, argv[3]);
    JS_ToFloat64(ctx, &start_angle, argv[4]);
    JS_ToFloat64(ctx, &sweep_angle, argv[5]);

    bool use_center = false;
    if (argc >= 7)
    {
        use_center = JS_ToBool(ctx, argv[6]);
    }

    skia_canvas_draw_arc(canvas, x, y, x + w, y + h,
                         start_angle, sweep_angle, use_center, paint);
    return JS_UNDEFINED;
}

static JSValue op_set_fill_color(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    uint32_t color = js_get_color(ctx, argv[0]);
    skia_paint_set_color(drawingDesk->active_paint, color);
    skia_paint_set_style(drawingDesk->active_paint, SKIA_PAINT_FILL);
    return JS_UNDEFINED;
}

static JSValue op_set_stroke_color(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    uint32_t color = js_get_color(ctx, argv[0]);
    skia_paint_set_color(drawingDesk->active_paint, color);
    skia_paint_set_style(drawingDesk->active_paint, SKIA_PAINT_STROKE);
    return JS_UNDEFINED;
}

static JSValue op_set_stroke_width(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    double width;
    JS_ToFloat64(ctx, &width, argv[0]);
    skia_paint_set_stroke_width(drawingDesk->active_paint, width);
    return JS_UNDEFINED;
}

static JSValue op_set_anti_alias(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    bool enabled = JS_ToBool(ctx, argv[0]);
    skia_paint_set_anti_alias(drawingDesk->active_paint, enabled);
    return JS_UNDEFINED;
}

static JSValue op_set_alpha(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    double alpha;
    JS_ToFloat64(ctx, &alpha, argv[0]);
    
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    skia_paint_set_alpha(drawingDesk->active_paint, (uint8_t)alpha);
    return JS_UNDEFINED;
}

static JSValue op_set_stroke_cap(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    const char *cap_str = JS_ToCString(ctx, argv[0]);
    if (cap_str)
    {
        SkiaStrokeCap cap = SKIA_STROKE_CAP_BUTT;
        if (strcmp(cap_str, "round") == 0)
            cap = SKIA_STROKE_CAP_ROUND;
        else if (strcmp(cap_str, "square") == 0)
            cap = SKIA_STROKE_CAP_SQUARE;
        skia_paint_set_stroke_cap(drawingDesk->active_paint, cap);
        JS_FreeCString(ctx, cap_str);
    }
    return JS_UNDEFINED;
}

static JSValue op_set_stroke_join(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_UNDEFINED;

    const char *join_str = JS_ToCString(ctx, argv[0]);
    if (join_str)
    {
        SkiaStrokeJoin join = SKIA_STROKE_JOIN_MITER;
        if (strcmp(join_str, "round") == 0)
            join = SKIA_STROKE_JOIN_ROUND;
        else if (strcmp(join_str, "bevel") == 0)
            join = SKIA_STROKE_JOIN_BEVEL;
        skia_paint_set_stroke_join(drawingDesk->active_paint, join);
        JS_FreeCString(ctx, join_str);
    }
    return JS_UNDEFINED;
}

static JSValue op_set_blend_mode(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint || argc < 1)
        return JS_ThrowTypeError(ctx, "sys.canvas.setBlendMode: missing something");

    const char *mode_str = JS_ToCString(ctx, argv[0]);
    if (!mode_str)
        return JS_ThrowTypeError(ctx, "sys.canvas.setBlendMode: mode must be a string");

    for (size_t i = 0; i < sizeof(k_blend_mode_table) / sizeof(k_blend_mode_table[0]); i++)
    {
        if (strcmp(mode_str, k_blend_mode_table[i].name) == 0)
        {
            skia_paint_set_blend_mode(drawingDesk->active_paint, k_blend_mode_table[i].mode);
            JS_FreeCString(ctx, mode_str);
            return JS_TRUE;
        }
    }
    JSValue err = JS_ThrowRangeError(ctx, "sys.canvas.setBlendMode: unknown mode '%s'", mode_str);
    JS_FreeCString(ctx, mode_str);
    return err;
}

static JSValue op_set_image_filter(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint)
        return JS_ThrowTypeError(ctx, "sys.canvas.setImageFilter: missing something");

    bool clear = (argc < 1) || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]);
    const char *name = NULL;
    if (!clear)
    {
        name = JS_ToCString(ctx, argv[0]);
        if (!name)
            return JS_ThrowTypeError(ctx, "sys.canvas.setImageFilter: name must be a string");
        if (strcmp(name, "none") == 0)
            clear = true;
    }

    if (clear)
    {
        if (name)
            JS_FreeCString(ctx, name);
        skia_paint_clear_image_filter(drawingDesk->active_paint);
        return JS_TRUE;
    }

    if (strcmp(name, "blur") == 0)
    {
        if (argc < 2)
        {
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx, "sys.canvas.setImageFilter('blur', sigmaX, [sigmaY]): missing sigmaX");
        }
        double sigma_x = 0.0, sigma_y;
        if (JS_ToFloat64(ctx, &sigma_x, argv[1]) < 0)
        {
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx, "sys.canvas.setImageFilter('blur'): sigmaX must be a number");
        }
        sigma_y = sigma_x;
        if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]))
        {
            if (JS_ToFloat64(ctx, &sigma_y, argv[2]) < 0)
            {
                JS_FreeCString(ctx, name);
                return JS_ThrowTypeError(ctx, "sys.canvas.setImageFilter('blur'): sigmaY must be a number");
            }
        }
        skia_paint_set_blur_filter(drawingDesk->active_paint,
                                   (float)sigma_x, (float)sigma_y);
        JS_FreeCString(ctx, name);
        return JS_TRUE;
    }

    if (strcmp(name, "drop-shadow") == 0 || strcmp(name, "drop-shadow-only") == 0)
    {
        bool only = (strcmp(name, "drop-shadow-only") == 0);
        
        if (argc < 5)
        {
            JSValue err = JS_ThrowTypeError(ctx,
                                            only
                                                ? "sys.canvas.setImageFilter('drop-shadow-only', dx, dy, sigmaX, [sigmaY], color): too few arguments"
                                                : "sys.canvas.setImageFilter('drop-shadow', dx, dy, sigmaX, [sigmaY], color): too few arguments");
            JS_FreeCString(ctx, name);
            return err;
        }
        double dx = 0.0, dy = 0.0, sigma_x = 0.0, sigma_y;
        if (JS_ToFloat64(ctx, &dx, argv[1]) < 0 ||
            JS_ToFloat64(ctx, &dy, argv[2]) < 0 ||
            JS_ToFloat64(ctx, &sigma_x, argv[3]) < 0)
        {
            JSValue err = JS_ThrowTypeError(ctx,
                                            only
                                                ? "sys.canvas.setImageFilter('drop-shadow-only'): dx/dy/sigmaX must be numbers"
                                                : "sys.canvas.setImageFilter('drop-shadow'): dx/dy/sigmaX must be numbers");
            JS_FreeCString(ctx, name);
            return err;
        }

        JSValue color_val;
        if (argc == 5)
        {
            sigma_y = sigma_x;
            color_val = argv[4];
        }
        else
        {
            if (JS_ToFloat64(ctx, &sigma_y, argv[4]) < 0)
            {
                JSValue err = JS_ThrowTypeError(ctx,
                                                only
                                                    ? "sys.canvas.setImageFilter('drop-shadow-only'): sigmaY must be a number"
                                                    : "sys.canvas.setImageFilter('drop-shadow'): sigmaY must be a number");
                JS_FreeCString(ctx, name);
                return err;
            }
            color_val = argv[5];
        }
        uint32_t color = js_get_color(ctx, color_val);
        if (only)
            skia_paint_set_drop_shadow_only_filter(drawingDesk->active_paint,
                                                   (float)dx, (float)dy,
                                                   (float)sigma_x, (float)sigma_y,
                                                   color);
        else
            skia_paint_set_drop_shadow_filter(drawingDesk->active_paint,
                                              (float)dx, (float)dy,
                                              (float)sigma_x, (float)sigma_y,
                                              color);
        JS_FreeCString(ctx, name);
        return JS_TRUE;
    }

    JSValue err = JS_ThrowRangeError(ctx, "sys.canvas.setImageFilter: unknown filter '%s'", name);
    JS_FreeCString(ctx, name);
    return err;
}

static JSValue op_set_color_filter(JSContext *ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    if (!drawingDesk || !drawingDesk->active_paint)
        return JS_ThrowTypeError(ctx, "sys.canvas.setImageFilter: missing something");

    bool clear = (argc < 1) || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]);
    const char *name = NULL;
    if (!clear)
    {
        name = JS_ToCString(ctx, argv[0]);
        if (!name)
            return JS_ThrowTypeError(ctx, "sys.canvas.setColorFilter: name must be a string");
        if (strcmp(name, "none") == 0)
            clear = true;
    }

    if (clear)
    {
        if (name)
            JS_FreeCString(ctx, name);
        skia_paint_clear_color_filter(drawingDesk->active_paint);
        return JS_TRUE;
    }

    if (strcmp(name, "matrix") == 0)
    {
        if (argc < 2)
        {
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx,
                                     "sys.canvas.setColorFilter('matrix', m): missing 20-element matrix");
        }

        JSValue len_v = JS_GetPropertyStr(ctx, argv[1], "length");
        if (JS_IsException(len_v) || JS_IsUndefined(len_v))
        {
            JS_FreeValue(ctx, len_v);
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx,
                                     "sys.canvas.setColorFilter('matrix'): expected an array-like of 20 numbers");
        }
        uint32_t len = 0;
        if (JS_ToUint32(ctx, &len, len_v) < 0)
        {
            JS_FreeValue(ctx, len_v);
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx,
                                     "sys.canvas.setColorFilter('matrix'): could not read array length");
        }
        JS_FreeValue(ctx, len_v);
        if (len != 20)
        {
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx,
                                     "sys.canvas.setColorFilter('matrix'): matrix must have exactly 20 elements (got %u)",
                                     (unsigned)len);
        }
        float m[20];
        for (uint32_t i = 0; i < 20; i++)
        {
            JSValue v = JS_GetPropertyUint32(ctx, argv[1], i);
            double d = 0.0;
            int rc = JS_ToFloat64(ctx, &d, v);
            JS_FreeValue(ctx, v);
            if (rc < 0)
            {
                JS_FreeCString(ctx, name);
                return JS_ThrowTypeError(ctx,
                                         "sys.canvas.setColorFilter('matrix'): element %u is not a number",
                                         (unsigned)i);
            }
            m[i] = (float)d;
        }
        skia_paint_set_color_matrix_filter(drawingDesk->active_paint, m);
        JS_FreeCString(ctx, name);
        return JS_TRUE;
    }

    if (strcmp(name, "blend") == 0)
    {
        if (argc < 3)
        {
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx,
                                     "sys.canvas.setColorFilter('blend', color, mode): too few arguments");
        }
        uint32_t color = js_get_color(ctx, argv[1]);
        const char *mode_str = JS_ToCString(ctx, argv[2]);
        if (!mode_str)
        {
            JS_FreeCString(ctx, name);
            return JS_ThrowTypeError(ctx,
                                     "sys.canvas.setColorFilter('blend'): mode must be a string");
        }
        SkiaBlendMode mode = SKIA_BLEND_MODE_SRC_OVER;
        bool found = false;
        for (size_t i = 0; i < sizeof(k_blend_mode_table) / sizeof(k_blend_mode_table[0]); i++)
        {
            if (strcmp(mode_str, k_blend_mode_table[i].name) == 0)
            {
                mode = k_blend_mode_table[i].mode;
                found = true;
                break;
            }
        }
        if (!found)
        {
            JSValue err = JS_ThrowRangeError(ctx,
                                             "sys.canvas.setColorFilter('blend'): unknown blend mode '%s'", mode_str);
            JS_FreeCString(ctx, mode_str);
            JS_FreeCString(ctx, name);
            return err;
        }
        JS_FreeCString(ctx, mode_str);
        skia_paint_set_blend_color_filter(drawingDesk->active_paint, color, mode);
        JS_FreeCString(ctx, name);
        return JS_TRUE;
    }

    JSValue err = JS_ThrowRangeError(ctx, "sys.canvas.setColorFilter: unknown filter '%s'", name);
    JS_FreeCString(ctx, name);
    return err;
}

static JSValue op_canvas_set_font(JSContext *ctx, JSGraphicContext *graphic_ctx, SkiaDrawingDesk *drawingDesk, int argc, JSValue *argv)
{
    const char *name;
    int index;

    if (!graphic_ctx)
        return JS_FALSE;

    if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
    {
        drawingDesk->active_font = NULL;
        return JS_TRUE;
    }

    name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;

    index = js_find_font_index(graphic_ctx, name);
    JS_FreeCString(ctx, name);
    if (index < 0)
    {
        return JS_ThrowReferenceError(ctx, "Unknown font");
    }

    drawingDesk->active_font = graphic_ctx->fonts[index];

    return JS_TRUE;
}

static JSValue js_canvas_clear(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_clear(ctx, graphic_ctx->drawingDesk.canvas, argc, argv);
}

static JSValue js_canvas_read_pixels(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_read_pixels(ctx, graphic_ctx->drawingDesk.canvas, argc, argv);
}

static JSValue js_canvas_draw_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_draw_rect(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_draw_round_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_draw_round_rect(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_draw_circle(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_draw_circle(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_draw_oval(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_draw_oval(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_draw_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_UNDEFINED;

    return op_canvas_draw_text(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, graphic_ctx->drawingDesk.active_font, argc, argv);
}

static JSValue js_canvas_draw_line(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_UNDEFINED;

    return op_canvas_draw_line(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_draw_point(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_UNDEFINED;

    return op_canvas_draw_point(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_measure_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_UNDEFINED;

    return op_canvas_measure_text(ctx, graphic_ctx->drawingDesk.active_font, argc, argv);
}

static JSValue js_canvas_measure_text_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewFloat64(ctx, 0.0));
        return obj;
    }

    return op_canvas_measure_text_rect(ctx, graphic_ctx->drawingDesk.active_font, argc, argv);
}

static JSValue js_canvas_load_font(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    char resolved_path[2048];
    const char *path;
    const char *name = NULL;
    const char *font_name;
    SkiaFont *font;
    char *stored_name;
    int index;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_FALSE;

    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;

    if (argc >= 2)
    {
        name = JS_ToCString(ctx, argv[1]);
        if (!name)
        {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
    }

    if (!js_resolve_project_path(ctx, path, resolved_path, sizeof(resolved_path)))
    {
        if (name)
            JS_FreeCString(ctx, name);
        JS_FreeCString(ctx, path);
        return JS_ThrowInternalError(ctx, "Font path is too long: %s", path);
    }

    font = skia_font_load_file(resolved_path);
    if (!font)
    {
        if (name)
            JS_FreeCString(ctx, name);
        JS_FreeCString(ctx, path);
        return JS_ThrowInternalError(ctx, "Failed to load font: %s", resolved_path);
    }

    font_name = name ? name : path;
    index = js_find_font_index(graphic_ctx, font_name);
    if (index >= 0)
    {
        if (graphic_ctx->drawingDesk.active_font == graphic_ctx->fonts[index])
        {
            graphic_ctx->drawingDesk.active_font = font;
        }
        skia_font_destroy(graphic_ctx->fonts[index]);
        graphic_ctx->fonts[index] = font;
    }
    else
    {
        if (!js_ensure_font_capacity(graphic_ctx))
        {
            skia_font_destroy(font);
            if (name)
                JS_FreeCString(ctx, name);
            JS_FreeCString(ctx, path);
            return JS_ThrowOutOfMemory(ctx);
        }

        stored_name = js_strdup_local(font_name);
        if (!stored_name)
        {
            skia_font_destroy(font);
            if (name)
                JS_FreeCString(ctx, name);
            JS_FreeCString(ctx, path);
            return JS_ThrowOutOfMemory(ctx);
        }

        graphic_ctx->fonts[graphic_ctx->font_count] = font;
        graphic_ctx->font_names[graphic_ctx->font_count] = stored_name;
        graphic_ctx->font_count += 1;
    }

    if (name)
        JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, path);
    return JS_TRUE;
}

static JSValue js_canvas_load_font_from_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *name = NULL;
    const char *font_name;
    SkiaFont *font;
    char *stored_name;
    int index;
    size_t byte_count = 0;
    JSValue ab_ref = JS_UNDEFINED;
    const uint8_t *data;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    (void)this_val;
    if (argc < 1)
        return JS_FALSE;

    data = js_get_buffer_bytes(ctx, argv[0], &byte_count, &ab_ref);
    if (!data)
        return JS_ThrowTypeError(ctx, "font.loadFromBuffer requires an ArrayBuffer or TypedArray");

    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
    {
        name = JS_ToCString(ctx, argv[1]);
        if (!name)
        {
            if (!JS_IsUndefined(ab_ref))
                JS_FreeValue(ctx, ab_ref);
            return JS_EXCEPTION;
        }
    }

    font = skia_font_load_buffer(data, byte_count);
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    if (!font)
    {
        if (name)
            JS_FreeCString(ctx, name);
        return JS_ThrowInternalError(ctx, "Failed to load font from buffer");
    }

    font_name = name ? name : "buffer";
    index = js_find_font_index(graphic_ctx, font_name);
    if (index >= 0)
    {
        if (graphic_ctx->drawingDesk.active_font == graphic_ctx->fonts[index])
            graphic_ctx->drawingDesk.active_font = font;
        skia_font_destroy(graphic_ctx->fonts[index]);
        graphic_ctx->fonts[index] = font;
    }
    else
    {
        if (!js_ensure_font_capacity(graphic_ctx))
        {
            skia_font_destroy(font);
            if (name)
                JS_FreeCString(ctx, name);
            return JS_ThrowOutOfMemory(ctx);
        }

        stored_name = js_strdup_local(font_name);
        if (!stored_name)
        {
            skia_font_destroy(font);
            if (name)
                JS_FreeCString(ctx, name);
            return JS_ThrowOutOfMemory(ctx);
        }

        graphic_ctx->fonts[graphic_ctx->font_count] = font;
        graphic_ctx->font_names[graphic_ctx->font_count] = stored_name;
        graphic_ctx->font_count += 1;
    }

    if (name)
        JS_FreeCString(ctx, name);
    return JS_TRUE;
}

static JSValue js_canvas_set_font(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_set_font(ctx, graphic_ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_canvas_draw_arc(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_canvas_draw_arc(ctx, graphic_ctx->drawingDesk.canvas, graphic_ctx->drawingDesk.active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_clear(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaCanvas *canvas = window_canvas_texture_get_canvas(canvas_texture);

    return op_canvas_clear(ctx, canvas, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_rect(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_read_pixels(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_read_pixels(ctx, drawingDesk->canvas, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_round_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_round_rect(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_circle(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_circle(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_oval(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_oval(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_line(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_line(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_point(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_point(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_measure_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_measure_text(ctx, drawingDesk->active_font, argc, argv);
}

static JSValue js_canvas_texture_canvas_measure_text_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_measure_text_rect(ctx, drawingDesk->active_font, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_arc(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);

    return op_canvas_draw_arc(ctx, drawingDesk->canvas, drawingDesk->active_paint, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_fill_color(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_fill_color(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_stroke_color(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_stroke_color(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_stroke_width(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_stroke_width(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_anti_alias(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_anti_alias(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_alpha(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_alpha(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_stroke_cap(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_stroke_cap(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_stroke_join(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!drawingDesk)
        return JS_UNDEFINED;

    return op_set_stroke_join(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_draw_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!canvas_texture || !drawingDesk)
        return JS_EXCEPTION;

    return op_canvas_draw_text(ctx, drawingDesk->canvas, drawingDesk->active_paint, drawingDesk->active_font, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_blend_mode(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!canvas_texture || !drawingDesk)
        return JS_EXCEPTION;

    return op_set_blend_mode(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_image_filter(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!canvas_texture || !drawingDesk)
        return JS_EXCEPTION;

    return op_set_image_filter(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_color_filter(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!canvas_texture || !drawingDesk)
        return JS_EXCEPTION;

    return op_set_color_filter(ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_canvas_set_font(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    SkiaDrawingDesk *drawingDesk = window_canvas_texture_get_drawingdesk(canvas_texture);
    if (!canvas_texture || !drawingDesk)
        return JS_EXCEPTION;

    return op_canvas_set_font(ctx, graphic_ctx, drawingDesk, argc, argv);
}

static JSValue js_canvas_texture_resize(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    CanvasTexture *canvas_texture = js_get_canvas_texture_from_this(ctx, graphic_ctx, this_val);
    int width, height;

    if (!canvas_texture)
        return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "resize(width, height) requires two arguments");

    JS_ToInt32(ctx, &width, argv[0]);
    JS_ToInt32(ctx, &height, argv[1]);
    if (width <= 0 || height <= 0)
        return JS_ThrowRangeError(ctx, "CanvasTexture dimensions must be positive");

    if (!window_canvas_texture_resize(canvas_texture, width, height))
        return JS_ThrowInternalError(ctx, "Failed to resize CanvasTexture");

    js_canvas_texture_update_properties(ctx, this_val, canvas_texture);
    return JS_UNDEFINED;
}

static JSValue js_canvas_texture_destroy(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    int id = js_get_canvas_texture_id_from_value(ctx, this_val);
    CanvasTexture *canvas_texture = js_get_canvas_texture_by_id(graphic_ctx, id);
    (void)argc;
    (void)argv;

    if (!canvas_texture)
        return JS_UNDEFINED;

    window_canvas_texture_destroy(canvas_texture);
    graphic_ctx->canvas_textures[id - 1] = NULL;
    js_canvas_texture_set_id(ctx, this_val, 0);
    JS_SetPropertyStr(ctx, this_val, "texture", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, this_val, "target", JS_NewInt32(ctx, 0));
    return JS_UNDEFINED;
}

static const JsGraphicFunction js_canvas_texture_canvas_funcs[] = {
    {"clear", 1, js_canvas_texture_canvas_clear},
    {"drawRect", 4, js_canvas_texture_canvas_draw_rect},
    {"drawCircle", 3, js_canvas_texture_canvas_draw_circle},
    {"drawText", 4, js_canvas_texture_canvas_draw_text},
    {"drawRoundRect", 6, js_canvas_texture_canvas_draw_round_rect},
    {"drawOval", 4, js_canvas_texture_canvas_draw_oval},
    {"drawLine", 4, js_canvas_texture_canvas_draw_line},
    {"drawPoint", 2, js_canvas_texture_canvas_draw_point},
    {"drawArc", 7, js_canvas_texture_canvas_draw_arc},
    {"setFillColor", 1, js_canvas_texture_canvas_set_fill_color},
    {"setStrokeColor", 1, js_canvas_texture_canvas_set_stroke_color},
    {"setStrokeWidth", 1, js_canvas_texture_canvas_set_stroke_width},
    {"setAntiAlias", 1, js_canvas_texture_canvas_set_anti_alias},
    {"setAlpha", 1, js_canvas_texture_canvas_set_alpha},
    {"setStrokeCap", 1, js_canvas_texture_canvas_set_stroke_cap},
    {"setStrokeJoin", 1, js_canvas_texture_canvas_set_stroke_join},
    {"measureText", 2, js_canvas_texture_canvas_measure_text},
    {"measureTextRect", 2, js_canvas_texture_canvas_measure_text_rect},
    {"readPixels", 0, js_canvas_texture_canvas_read_pixels},
    {"setBlendMode", 1, js_canvas_texture_canvas_set_blend_mode},
    {"setImageFilter", 2, js_canvas_texture_canvas_set_image_filter},
    {"setColorFilter", 2, js_canvas_texture_canvas_set_color_filter},
    {"setFont", 1, js_canvas_texture_canvas_set_font},
};

static JSValue js_graphics_create_canvas_texture(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int width, height, id;
    CanvasTexture *canvas_texture;
    JSValue obj;
    JSValue canvas_obj;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "createCanvasTexture(width, height) requires two arguments");

    JS_ToInt32(ctx, &width, argv[0]);
    JS_ToInt32(ctx, &height, argv[1]);
    if (width <= 0 || height <= 0)
        return JS_ThrowRangeError(ctx, "CanvasTexture dimensions must be positive");

    canvas_texture = window_canvas_texture_create(width, height);
    if (!canvas_texture)
        return JS_ThrowInternalError(ctx, "Failed to create CanvasTexture");

    id = js_register_canvas_texture(graphic_ctx, canvas_texture);
    if (id <= 0)
    {
        window_canvas_texture_destroy(canvas_texture);
        return JS_ThrowOutOfMemory(ctx);
    }

    canvas_obj = JS_NewObject(ctx);
    js_canvas_texture_set_id(ctx, canvas_obj, id);
    for (size_t i = 0; i < sizeof(js_canvas_texture_canvas_funcs) / sizeof(js_canvas_texture_canvas_funcs[0]); i++)
    {
        js_graphic_register_function(ctx, canvas_obj, &js_canvas_texture_canvas_funcs[i], graphic_ctx);
    }

    obj = JS_NewObject(ctx);
    js_canvas_texture_set_id(ctx, obj, id);
    js_canvas_texture_update_properties(ctx, obj, canvas_texture);
    JS_SetPropertyStr(ctx, obj, "id", JS_NewInt32(ctx, id));
    for (size_t i = 0; i < sizeof(js_canvas_texture_funcs) / sizeof(js_canvas_texture_funcs[0]); i++)
    {
        js_graphic_register_function(ctx, obj, &js_canvas_texture_funcs[i], graphic_ctx);
    }
    JS_SetPropertyStr(ctx, obj, "canvas", canvas_obj);

    return obj;
}

static JSValue js_set_fill_color(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_fill_color(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_stroke_color(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_stroke_color(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_stroke_width(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_stroke_width(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_anti_alias(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_anti_alias(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_alpha(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_alpha(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_stroke_cap(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_stroke_cap(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_stroke_join(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_stroke_join(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static const BlendModeEntry k_blend_mode_table[] = {
    {"src-over", SKIA_BLEND_MODE_SRC_OVER},
    {"src", SKIA_BLEND_MODE_SRC},
    {"dst-over", SKIA_BLEND_MODE_DST_OVER},
    {"dst-in", SKIA_BLEND_MODE_DST_IN},
    {"dst-out", SKIA_BLEND_MODE_DST_OUT},
    {"src-in", SKIA_BLEND_MODE_SRC_IN},
    {"src-out", SKIA_BLEND_MODE_SRC_OUT},
    {"clear", SKIA_BLEND_MODE_CLEAR},
    {"plus", SKIA_BLEND_MODE_PLUS},
    {"multiply", SKIA_BLEND_MODE_MULTIPLY},
    {"screen", SKIA_BLEND_MODE_SCREEN},
    {"overlay", SKIA_BLEND_MODE_OVERLAY},
    {"darken", SKIA_BLEND_MODE_DARKEN},
    {"lighten", SKIA_BLEND_MODE_LIGHTEN},
    {"color-dodge", SKIA_BLEND_MODE_COLOR_DODGE},
    {"color-burn", SKIA_BLEND_MODE_COLOR_BURN},
    {"hard-light", SKIA_BLEND_MODE_HARD_LIGHT},
    {"soft-light", SKIA_BLEND_MODE_SOFT_LIGHT},
    {"difference", SKIA_BLEND_MODE_DIFFERENCE},
    {"exclusion", SKIA_BLEND_MODE_EXCLUSION},
    {"hue", SKIA_BLEND_MODE_HUE},
    {"saturation", SKIA_BLEND_MODE_SATURATION},
    {"color", SKIA_BLEND_MODE_COLOR},
    {"luminosity", SKIA_BLEND_MODE_LUMINOSITY},
};

static JSValue js_set_blend_mode(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_blend_mode(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_image_filter(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_image_filter(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static JSValue js_set_color_filter(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    return op_set_color_filter(ctx, &(graphic_ctx->drawingDesk), argc, argv);
}

static int add_path(JSGraphicContext *graphic_ctx, SkiaPath *path)
{
    if (graphic_ctx->path_count >= graphic_ctx->path_capacity)
    {
        int new_capacity = graphic_ctx->path_capacity == 0 ? 16 : graphic_ctx->path_capacity * 2;
        SkiaPath **new_paths = (SkiaPath **)realloc(graphic_ctx->paths, new_capacity * sizeof(SkiaPath *));
        if (!new_paths)
            return -1;
        graphic_ctx->paths = new_paths;
        graphic_ctx->path_capacity = new_capacity;
    }
    int id = graphic_ctx->path_count;
    graphic_ctx->paths[graphic_ctx->path_count++] = path;
    return id;
}

static SkiaPath *get_path(JSGraphicContext *graphic_ctx, int id)
{
    if (id < 0 || id >= graphic_ctx->path_count)
        return NULL;
    return graphic_ctx->paths[id];
}

static JSValue js_path_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    SkiaPath *path = skia_path_create();
    if (!path)
        return JS_UNDEFINED;

    int id = add_path(graphic_ctx, path);
    if (id < 0)
    {
        skia_path_destroy(path);
        return JS_UNDEFINED;
    }

    return JS_NewInt32(ctx, id);
}

static JSValue js_path_reset(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_reset(path);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_move_to(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 3)
        return JS_UNDEFINED;

    int id;
    double x, y;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_move_to(path, x, y);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_line_to(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 3)
        return JS_UNDEFINED;

    int id;
    double x, y;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_line_to(path, x, y);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_quad_to(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 5)
        return JS_UNDEFINED;

    int id;
    double x1, y1, x2, y2;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &x1, argv[1]);
    JS_ToFloat64(ctx, &y1, argv[2]);
    JS_ToFloat64(ctx, &x2, argv[3]);
    JS_ToFloat64(ctx, &y2, argv[4]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_quad_to(path, x1, y1, x2, y2);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_cubic_to(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 7)
        return JS_UNDEFINED;

    int id;
    double x1, y1, x2, y2, x3, y3;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &x1, argv[1]);
    JS_ToFloat64(ctx, &y1, argv[2]);
    JS_ToFloat64(ctx, &x2, argv[3]);
    JS_ToFloat64(ctx, &y2, argv[4]);
    JS_ToFloat64(ctx, &x3, argv[5]);
    JS_ToFloat64(ctx, &y3, argv[6]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_cubic_to(path, x1, y1, x2, y2, x3, y3);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_close(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_close(path);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_add_rect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 5)
        return JS_UNDEFINED;

    int id;
    double x, y, w, h;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);
    JS_ToFloat64(ctx, &w, argv[3]);
    JS_ToFloat64(ctx, &h, argv[4]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_add_rect(path, x, y, x + w, y + h);
    }
    return JS_UNDEFINED;
}

static JSValue js_path_add_circle(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 4)
        return JS_UNDEFINED;

    int id;
    double cx, cy, radius;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &cx, argv[1]);
    JS_ToFloat64(ctx, &cy, argv[2]);
    JS_ToFloat64(ctx, &radius, argv[3]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_path_add_circle(path, cx, cy, radius);
    }
    return JS_UNDEFINED;
}

static JSValue js_canvas_draw_path(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    SkiaPath *path = get_path(graphic_ctx, id);
    if (path)
    {
        skia_canvas_draw_path(graphic_ctx->drawingDesk.canvas, path, graphic_ctx->drawingDesk.active_paint);
    }
    return JS_UNDEFINED;
}

static int add_svg(JSGraphicContext *graphic_ctx, SkiaSVG *svg)
{
    if (graphic_ctx->svg_count >= graphic_ctx->svg_capacity)
    {
        int new_capacity = graphic_ctx->svg_capacity == 0 ? 8 : graphic_ctx->svg_capacity * 2;
        SkiaSVG **new_svgs = (SkiaSVG **)realloc(graphic_ctx->svgs, new_capacity * sizeof(SkiaSVG *));
        if (!new_svgs)
            return -1;
        graphic_ctx->svgs = new_svgs;
        graphic_ctx->svg_capacity = new_capacity;
    }
    int id = graphic_ctx->svg_count;
    graphic_ctx->svgs[graphic_ctx->svg_count++] = svg;
    return id;
}

static SkiaSVG *get_svg(JSGraphicContext *graphic_ctx, int id)
{
    if (id < 0 || id >= graphic_ctx->svg_count)
        return NULL;
    return graphic_ctx->svgs[id];
}

static JSValue js_canvas_load_svg(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_NewInt32(ctx, -1);

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_NewInt32(ctx, -1);

    char resolved[1024];
    if (!js_resolve_project_path(ctx, path, resolved, sizeof(resolved)))
    {
        JS_FreeCString(ctx, path);
        return JS_NewInt32(ctx, -1);
    }
    JS_FreeCString(ctx, path);

    SkiaSVG *svg = skia_svg_load_file(resolved);
    if (!svg)
        return JS_NewInt32(ctx, -1);

    int id = add_svg(graphic_ctx, svg);
    if (id < 0)
    {
        skia_svg_destroy(svg);
        return JS_NewInt32(ctx, -1);
    }

    return JS_NewInt32(ctx, id);
}

static JSValue js_canvas_load_svg_from_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    size_t byte_count = 0;
    JSValue ab_ref = JS_UNDEFINED;
    const uint8_t *data;
    SkiaSVG *svg;
    int id;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_NewInt32(ctx, -1);

    data = js_get_buffer_bytes(ctx, argv[0], &byte_count, &ab_ref);
    if (!data)
        return JS_ThrowTypeError(ctx, "svg.loadFromBuffer requires an ArrayBuffer or TypedArray");

    svg = skia_svg_load_buffer(data, byte_count);
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    if (!svg)
        return JS_NewInt32(ctx, -1);

    id = add_svg(graphic_ctx, svg);
    if (id < 0)
    {
        skia_svg_destroy(svg);
        return JS_NewInt32(ctx, -1);
    }

    return JS_NewInt32(ctx, id);
}

static JSValue js_runtime_destroy_svg(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    SkiaSVG *svg = get_svg(graphic_ctx, id);
    if (svg)
    {
        skia_svg_destroy(svg);
        graphic_ctx->svgs[id] = NULL;
    }
    return JS_UNDEFINED;
}

static JSValue js_canvas_draw_svg(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 5)
        return JS_UNDEFINED;

    int id;
    double x, y, w, h;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);
    JS_ToFloat64(ctx, &w, argv[3]);
    JS_ToFloat64(ctx, &h, argv[4]);

    SkiaSVG *svg = get_svg(graphic_ctx, id);
    if (svg)
    {
        skia_svg_render(svg, graphic_ctx->drawingDesk.canvas, x, y, w, h);
    }
    return JS_UNDEFINED;
}

static JSValue js_canvas_get_svg_width(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_NewFloat64(ctx, 0);

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    SkiaSVG *svg = get_svg(graphic_ctx, id);
    if (svg)
        return JS_NewFloat64(ctx, skia_svg_get_width(svg));

    return JS_NewFloat64(ctx, 0);
}

static JSValue js_canvas_get_svg_height(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argc < 1)
        return JS_NewFloat64(ctx, 0);

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    SkiaSVG *svg = get_svg(graphic_ctx, id);
    if (svg)
        return JS_NewFloat64(ctx, skia_svg_get_height(svg));

    return JS_NewFloat64(ctx, 0);
}

static JSValue js_request_animation_frame(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 1)
        return JS_UNDEFINED;

    if (JS_IsFunction(ctx, argv[0]))
    {
        
        if (graphic_ctx->has_animation_callback)
        {
            JS_FreeValue(ctx, graphic_ctx->animation_callback);
        }

        graphic_ctx->animation_callback = JS_DupValue(ctx, argv[0]);
        graphic_ctx->has_animation_callback = true;
    }

    return JS_NewInt32(ctx, 1); 
}

static JSValue js_cancel_animation_frame(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (graphic_ctx->has_animation_callback)
    {
        JS_FreeValue(ctx, graphic_ctx->animation_callback);
        graphic_ctx->animation_callback = JS_UNDEFINED;
        graphic_ctx->has_animation_callback = false;
    }

    return JS_UNDEFINED;
}

static JSValue js_get_width(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    return JS_NewInt32(ctx, graphic_ctx->width);
}

static JSValue js_get_height(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    return JS_NewInt32(ctx, graphic_ctx->height);
}

static JSValue js_get_display_density(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    return JS_NewFloat64(ctx, (double)graphic_ctx->display_density);
}

static JSValue js_create_pointer_object(JSContext *ctx, int id, int x, int y, int dx, int dy,
                                        bool down, bool pressed, const char *type, int magic)
{
    JSValue pointer = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, pointer, "id", JS_NewInt32(ctx, id));
    JS_SetPropertyStr(ctx, pointer, "x", JS_NewInt32(ctx, x));
    JS_SetPropertyStr(ctx, pointer, "y", JS_NewInt32(ctx, y));
    JS_SetPropertyStr(ctx, pointer, "dx", JS_NewInt32(ctx, dx));
    JS_SetPropertyStr(ctx, pointer, "dy", JS_NewInt32(ctx, dy));
    JS_SetPropertyStr(ctx, pointer, "down", JS_NewBool(ctx, down));
    JS_SetPropertyStr(ctx, pointer, "pressed", JS_NewBool(ctx, pressed));
    JS_SetPropertyStr(ctx, pointer, "type", JS_NewString(ctx, type));

    return pointer;
}

static JSValue js_get_input(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->input)
        return JS_ThrowInternalError(ctx, "No active input");

    InputState *input = graphic_ctx->input;

    JSValue obj = JS_NewObject(ctx);

    JSValue mouse = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, mouse, "x", JS_NewInt32(ctx, input->mouse_x));
    JS_SetPropertyStr(ctx, mouse, "y", JS_NewInt32(ctx, input->mouse_y));
    JS_SetPropertyStr(ctx, mouse, "dx", JS_NewInt32(ctx, input->mouse_dx));
    JS_SetPropertyStr(ctx, mouse, "dy", JS_NewInt32(ctx, input->mouse_dy));
    JS_SetPropertyStr(ctx, mouse, "wheelX", JS_NewInt32(ctx, input->mouse_wheel_x));
    JS_SetPropertyStr(ctx, mouse, "wheelY", JS_NewInt32(ctx, input->mouse_wheel_y));
    JS_SetPropertyStr(ctx, mouse, "left", JS_NewBool(ctx, input->mouse_buttons[INPUT_MOUSE_LEFT]));
    JS_SetPropertyStr(ctx, mouse, "middle", JS_NewBool(ctx, input->mouse_buttons[INPUT_MOUSE_MIDDLE]));
    JS_SetPropertyStr(ctx, mouse, "right", JS_NewBool(ctx, input->mouse_buttons[INPUT_MOUSE_RIGHT]));
    JS_SetPropertyStr(ctx, mouse, "leftPressed", JS_NewBool(ctx, input->mouse_buttons_pressed[INPUT_MOUSE_LEFT]));
    JS_SetPropertyStr(ctx, mouse, "rightPressed", JS_NewBool(ctx, input->mouse_buttons_pressed[INPUT_MOUSE_RIGHT]));
    JS_SetPropertyStr(ctx, obj, "mouse", mouse);

    JS_SetPropertyStr(ctx, obj, "pointer",
                      js_create_pointer_object(ctx,
                                               0,
                                               input->mouse_x,
                                               input->mouse_y,
                                               input->mouse_dx,
                                               input->mouse_dy,
                                               input->mouse_buttons[INPUT_MOUSE_LEFT],
                                               input->mouse_buttons_pressed[INPUT_MOUSE_LEFT],
                                               input_touch_count(input) > 0 ? "touch" : "mouse", magic));

    JSValue pointers = JS_NewArray(ctx);
    int touch_count = input_touch_count(input);
    if (touch_count > 0)
    {
        for (int i = 0; i < touch_count; i++)
        {
            const InputTouchPoint *touch = input_touch_get(input, i);
            if (!touch)
                continue;

            JS_SetPropertyUint32(ctx, pointers, (uint32_t)i,
                                 js_create_pointer_object(ctx,
                                                          touch->id,
                                                          touch->x,
                                                          touch->y,
                                                          touch->dx,
                                                          touch->dy,
                                                          true,
                                                          touch->pressed,
                                                          "touch", magic));
        }
    }
    else if (input->mouse_buttons[INPUT_MOUSE_LEFT])
    {
        JS_SetPropertyUint32(ctx, pointers, 0,
                             js_create_pointer_object(ctx,
                                                      0,
                                                      input->mouse_x,
                                                      input->mouse_y,
                                                      input->mouse_dx,
                                                      input->mouse_dy,
                                                      true,
                                                      input->mouse_buttons_pressed[INPUT_MOUSE_LEFT],
                                                      "mouse", magic));
    }
    JS_SetPropertyStr(ctx, obj, "pointers", pointers);

    JSValue keyboard = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, keyboard, "shift", JS_NewBool(ctx, input->shift));
    JS_SetPropertyStr(ctx, keyboard, "ctrl", JS_NewBool(ctx, input->ctrl));
    JS_SetPropertyStr(ctx, keyboard, "alt", JS_NewBool(ctx, input->alt));
    JS_SetPropertyStr(ctx, keyboard, "meta", JS_NewBool(ctx, input->meta));
    JS_SetPropertyStr(ctx, obj, "keyboard", keyboard);

    JS_SetPropertyStr(ctx, obj, "text",
                      JS_NewStringLen(ctx, input->text, input->text_length));

    JSValue text_edit = JS_NULL;
    if (input->text_edit_changed)
    {
        text_edit = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, text_edit, "text",
                          JS_NewStringLen(ctx, input->text_value,
                                          input->text_value_length));
        JS_SetPropertyStr(ctx, text_edit, "selectionStart",
                          JS_NewInt32(ctx, input->text_selection_start));
        JS_SetPropertyStr(ctx, text_edit, "selectionEnd",
                          JS_NewInt32(ctx, input->text_selection_end));
    }
    JS_SetPropertyStr(ctx, obj, "textEdit", text_edit);

    JSValue composition = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, composition, "active",
                      JS_NewBool(ctx, input->composition_active));
    JS_SetPropertyStr(ctx, composition, "changed",
                      JS_NewBool(ctx, input->composition_changed));
    JS_SetPropertyStr(ctx, composition, "text",
                      JS_NewStringLen(ctx, input->composition_text,
                                      input->composition_text_length));
    JS_SetPropertyStr(ctx, composition, "selectionStart",
                      JS_NewInt32(ctx, input->composition_selection_start));
    JS_SetPropertyStr(ctx, composition, "selectionEnd",
                      JS_NewInt32(ctx, input->composition_selection_end));
    JS_SetPropertyStr(ctx, obj, "composition", composition);
    JS_SetPropertyStr(ctx, obj, "textInputActive",
                      JS_NewBool(ctx, input->text_session_active));

    JS_SetPropertyStr(ctx, obj, "deltaTime", JS_NewFloat64(ctx, input->delta_time));
    JS_SetPropertyStr(ctx, obj, "totalTime", JS_NewFloat64(ctx, input->total_time));
    JS_SetPropertyStr(ctx, obj, "frameCount", JS_NewInt64(ctx, input->frame_count));

    JS_SetPropertyStr(ctx, obj, "focused", JS_NewBool(ctx, input->window_focused));

    return obj;
}

static JSValue js_is_key_down(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->input)
        return JS_ThrowInternalError(ctx, "No active input");

    if (argc < 1)
        return JS_FALSE;

    int scancode;
    JS_ToInt32(ctx, &scancode, argv[0]);

    return JS_NewBool(ctx, input_key_down(graphic_ctx->input, scancode));
}

static JSValue js_is_key_pressed(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->input)
        return JS_ThrowInternalError(ctx, "No active input");

    if (argc < 1)
        return JS_FALSE;

    int scancode;
    JS_ToInt32(ctx, &scancode, argv[0]);

    return JS_NewBool(ctx, input_key_pressed(graphic_ctx->input, scancode));
}

static bool js_text_input_options(JSContext *ctx, JSValue options,
                                  const char **text, int *selection_start,
                                  int *selection_end, bool *multiline,
                                  int *x, int *y, int *width, int *height)
{
    JSValue value;
    if (!JS_IsObject(options))
    {
        JS_ThrowTypeError(ctx, "Text input options must be an object");
        return false;
    }
    value = JS_GetPropertyStr(ctx, options, "text");
    *text = JS_ToCString(ctx, value);
    JS_FreeValue(ctx, value);
    if (!*text)
        return false;
    value = JS_GetPropertyStr(ctx, options, "selectionStart");
    if (JS_ToInt32(ctx, selection_start, value) < 0)
    {
        JS_FreeValue(ctx, value);
        JS_FreeCString(ctx, *text);
        return false;
    }
    JS_FreeValue(ctx, value);
    value = JS_GetPropertyStr(ctx, options, "selectionEnd");
    if (JS_ToInt32(ctx, selection_end, value) < 0)
    {
        JS_FreeValue(ctx, value);
        JS_FreeCString(ctx, *text);
        return false;
    }
    JS_FreeValue(ctx, value);
    if (multiline)
    {
        value = JS_GetPropertyStr(ctx, options, "multiline");
        *multiline = JS_IsUndefined(value) ? false : JS_ToBool(ctx, value) == 1;
        JS_FreeValue(ctx, value);
    }
    if (x && y && width && height)
    {
        JSValue caret = JS_GetPropertyStr(ctx, options, "caret");
        if (JS_IsObject(caret))
        {
            value = JS_GetPropertyStr(ctx, caret, "x");
            JS_ToInt32(ctx, x, value);
            JS_FreeValue(ctx, value);
            value = JS_GetPropertyStr(ctx, caret, "y");
            JS_ToInt32(ctx, y, value);
            JS_FreeValue(ctx, value);
            value = JS_GetPropertyStr(ctx, caret, "width");
            JS_ToInt32(ctx, width, value);
            JS_FreeValue(ctx, value);
            value = JS_GetPropertyStr(ctx, caret, "height");
            JS_ToInt32(ctx, height, value);
            JS_FreeValue(ctx, value);
        }
        JS_FreeValue(ctx, caret);
    }
    return true;
}

static JSValue js_start_text_input(JSContext *ctx, JSValue this_val,
                                   int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *text;
    int start;
    int end;
    bool multiline;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->input)
        return JS_ThrowInternalError(ctx, "No active input");

    if (argc < 1 ||
        !js_text_input_options(ctx, argv[0], &text, &start, &end,
                               &multiline, NULL, NULL, NULL, NULL))
        return JS_EXCEPTION;
    bool result = input_text_start(graphic_ctx->input, text, start, end,
                                   multiline);
    JS_FreeCString(ctx, text);
    return JS_NewBool(ctx, result);
}

static JSValue js_update_text_input(JSContext *ctx, JSValue this_val,
                                    int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *text;
    int start;
    int end;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->input)
        return JS_ThrowInternalError(ctx, "No active input");

    if (argc < 1 ||
        !js_text_input_options(ctx, argv[0], &text, &start, &end,
                               NULL, &x, &y, &width, &height))
        return JS_EXCEPTION;
    bool result = input_text_update(graphic_ctx->input, text, start, end,
                                    x, y, width, height);
    JS_FreeCString(ctx, text);
    return JS_NewBool(ctx, result);
}

static JSValue js_stop_text_input(JSContext *ctx, JSValue this_val,
                                  int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->input)
        return JS_ThrowInternalError(ctx, "No active input");

    input_text_stop(graphic_ctx->input);

    return JS_UNDEFINED;
}

static JSValue transform_save(JSContext *ctx, JSValueConst this_value,
                              int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    skia_canvas_save(graphic_ctx->drawingDesk.canvas);

    return JS_UNDEFINED;
}

static JSValue transform_restore(JSContext *ctx, JSValueConst this_value,
                                 int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    skia_canvas_restore(graphic_ctx->drawingDesk.canvas);

    return JS_UNDEFINED;
}

static JSValue transform_translate(JSContext *ctx, JSValueConst this_value,
                                   int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argument_count < 2)
        return JS_UNDEFINED;

    double dx, dy;
    JS_ToFloat64(ctx, &dx, arguments[0]);
    JS_ToFloat64(ctx, &dy, arguments[1]);
    skia_canvas_translate(graphic_ctx->drawingDesk.canvas, dx, dy);
    return JS_UNDEFINED;
}

static JSValue transform_rotate(JSContext *ctx, JSValueConst this_value,
                                int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");

    if (argument_count < 3)
        return JS_UNDEFINED;

    double degrees;
    JS_ToFloat64(ctx, &degrees, arguments[0]);
    if (argument_count >= 3)
    {
        double px, py;
        JS_ToFloat64(ctx, &px, arguments[1]);
        JS_ToFloat64(ctx, &py, arguments[2]);
        skia_canvas_rotate_around(graphic_ctx->drawingDesk.canvas, degrees, px, py);
    }
    else
    {
        skia_canvas_rotate(graphic_ctx->drawingDesk.canvas, degrees);
    }
    return JS_UNDEFINED;
}

static JSValue transform_scale(JSContext *ctx, JSValueConst this_value,
                               int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    if (argument_count < 2)
        return JS_UNDEFINED;
    double sx, sy;
    JS_ToFloat64(ctx, &sx, arguments[0]);
    JS_ToFloat64(ctx, &sy, arguments[1]);
    skia_canvas_scale(graphic_ctx->drawingDesk.canvas, sx, sy);
    return JS_UNDEFINED;
}

static JSValue transform_skew(JSContext *ctx, JSValueConst this_value,
                              int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    if (argument_count < 2)
        return JS_UNDEFINED;
    double sx, sy;
    JS_ToFloat64(ctx, &sx, arguments[0]);
    JS_ToFloat64(ctx, &sy, arguments[1]);
    skia_canvas_skew(graphic_ctx->drawingDesk.canvas, sx, sy);
    return JS_UNDEFINED;
}

static JSValue transform_reset(JSContext *ctx, JSValueConst this_value,
                               int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    skia_canvas_reset_transform(graphic_ctx->drawingDesk.canvas);
    return JS_UNDEFINED;
}

static JSValue transform_clip_rect(JSContext *ctx, JSValueConst this_value,
                                   int argument_count, JSValueConst *arguments, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx || !graphic_ctx->drawingDesk.canvas)
        return JS_ThrowInternalError(ctx, "No active canvas");
    if (argument_count < 4)
        return JS_UNDEFINED;
    double x, y, width, height;
    JS_ToFloat64(ctx, &x, arguments[0]);
    JS_ToFloat64(ctx, &y, arguments[1]);
    JS_ToFloat64(ctx, &width, arguments[2]);
    JS_ToFloat64(ctx, &height, arguments[3]);
    skia_canvas_clip_rect(graphic_ctx->drawingDesk.canvas, x, y, x + width, y + height);
    return JS_UNDEFINED;
}

static const JsGraphicFunction js_sys_canvas_funcs[] = {
    {"clear", 1, js_canvas_clear},
    {"drawRect", 4, js_canvas_draw_rect},
    {"drawCircle", 3, js_canvas_draw_circle},
    {"drawText", 4, js_canvas_draw_text},
    {"readPixels", 0, js_canvas_read_pixels},
    {"drawRoundRect", 6, js_canvas_draw_round_rect},
    {"drawOval", 4, js_canvas_draw_oval},
    {"drawLine", 4, js_canvas_draw_line},
    {"drawPoint", 2, js_canvas_draw_point},
    {"drawPath", 1, js_canvas_draw_path},
    {"drawArc", 7, js_canvas_draw_arc},
    {"drawSvg", 5, js_canvas_draw_svg},
    {"measureText", 2, js_canvas_measure_text},
    {"measureTextRect", 2, js_canvas_measure_text_rect},
    {"setFillColor", 1, js_set_fill_color},
    {"setStrokeColor", 1, js_set_stroke_color},
    {"setStrokeWidth", 1, js_set_stroke_width},
    {"setAntiAlias", 1, js_set_anti_alias},
    {"setAlpha", 1, js_set_alpha},
    {"setStrokeCap", 1, js_set_stroke_cap},
    {"setStrokeJoin", 1, js_set_stroke_join},
    {"setBlendMode", 1, js_set_blend_mode},
    {"setImageFilter", 2, js_set_image_filter},
    {"setColorFilter", 2, js_set_color_filter},
    {"setFont", 1, js_canvas_set_font},
    {"save", 0, transform_save},
    {"restore", 0, transform_restore},
    {"translate", 2, transform_translate},
    {"rotate", 3, transform_rotate},
    {"scale", 2, transform_scale},
    {"skew", 2, transform_skew},
    {"reset", 0, transform_reset},
    {"clipRect", 4, transform_clip_rect},
};

static const JsGraphicFunction js_canvas_texture_funcs[] = {
    {"resize", 2, js_canvas_texture_resize},
    {"destroy", 0, js_canvas_texture_destroy},
};

static const JsGraphicFunction js_sys_graphics_funcs[] = {
    {"createCanvasTexture", 2, js_graphics_create_canvas_texture},
};

static const JsGraphicFunction js_sys_path_funcs[] = {
    {"create", 0, js_path_create},
    {"reset", 1, js_path_reset},
    {"moveTo", 3, js_path_move_to},
    {"lineTo", 3, js_path_line_to},
    {"quadTo", 5, js_path_quad_to},
    {"cubicTo", 7, js_path_cubic_to},
    {"close", 1, js_path_close},
    {"addRect", 5, js_path_add_rect},
    {"addCircle", 4, js_path_add_circle},
};

static const JsGraphicFunction js_sys_svg_funcs[] = {
    {"load", 1, js_canvas_load_svg},
    {"loadFromBuffer", 1, js_canvas_load_svg_from_buffer},
    {"destroy", 1, js_runtime_destroy_svg},
    {"getWidth", 1, js_canvas_get_svg_width},
    {"getHeight", 1, js_canvas_get_svg_height},
};

static const JsGraphicFunction js_sys_font_funcs[] = {
    {"load", 2, js_canvas_load_font},
    {"loadFromBuffer", 2, js_canvas_load_font_from_buffer},
};

static const JsGraphicFunction js_sys_input_funcs[] = {
    {"get", 0, js_get_input},
    {"isKeyDown", 1, js_is_key_down},
    {"isKeyPressed", 1, js_is_key_pressed},
    {"startTextInput", 1, js_start_text_input},
    {"updateTextInput", 1, js_update_text_input},
    {"stopTextInput", 0, js_stop_text_input},
};

static const JsGraphicFunction js_sys_window_funcs[] = {
    {"getWidth", 0, js_get_width},
    {"getHeight", 0, js_get_height},
    {"getDisplayDensity", 0, js_get_display_density},
};

static const JsGraphicFunction js_sys_animation_funcs[] = {
    {"requestFrame", 1, js_request_animation_frame},
    {"cancelFrame", 1, js_cancel_animation_frame},
};

JSValue addSysObjectMember(JSContext *context, JSGraphicContext *graphic_ctx, JSValue sys_obj, char *name, int size, JsGraphicFunction *functions)
{
    JSValue obj = JS_NewObject(context);
    for (size_t i = 0; i < size; i++)
    {
        js_graphic_register_function(context, obj, &functions[i], graphic_ctx);
    }
    JS_SetPropertyStr(context, sys_obj, name, obj);
    return obj;
}

#define ADD_SYS_OBJECT_MEMBER(obj, name, funcs) addSysObjectMember(ctx->context, graphic_ctx, obj, name, sizeof(funcs) / sizeof(funcs[0]), (JsGraphicFunction *)funcs);

JSGraphicContext *js_graphic_init(JSRuntimeContext *ctx)
{
    JSGraphicContext *graphic_ctx = (JSGraphicContext *)calloc(1, sizeof(JSGraphicContext));

    graphic_ctx->context = ctx->context;

    graphic_ctx->drawingDesk.active_paint = skia_paint_create();
    if (!graphic_ctx->drawingDesk.active_paint)
    {
        JS_SetContextOpaque(ctx->context, NULL);
        JS_SetRuntimeOpaque(ctx->runtime, NULL);
        JS_FreeContext(ctx->context);
        JS_FreeRuntime(ctx->runtime);
        free(ctx);
        return false;
    }

    graphic_ctx->animation_callback = JS_UNDEFINED;
    graphic_ctx->has_animation_callback = false;

    JSValue global = JS_GetGlobalObject(ctx->context);
    JSValue sys_obj = JS_GetPropertyStr(ctx->context, global, "sys");

    ADD_SYS_OBJECT_MEMBER(sys_obj, "canvas", js_sys_canvas_funcs);
    ADD_SYS_OBJECT_MEMBER(sys_obj, "path", js_sys_path_funcs);
    ADD_SYS_OBJECT_MEMBER(sys_obj, "svg", js_sys_svg_funcs);
    ADD_SYS_OBJECT_MEMBER(sys_obj, "font", js_sys_font_funcs);
    ADD_SYS_OBJECT_MEMBER(sys_obj, "graphics", js_sys_graphics_funcs);

    js_gl_register(ctx->context, graphic_ctx, sys_obj);

    JSValue input_obj = JS_NewObject(ctx->context);
    for (size_t i = 0; i < sizeof(js_sys_input_funcs) / sizeof(js_sys_input_funcs[0]); i++)
    {
        js_graphic_register_function(ctx->context, input_obj, &js_sys_input_funcs[i], graphic_ctx);
    }
    JS_SetPropertyStr(ctx->context, sys_obj, "input", input_obj);

    JSValue window_obj = JS_NewObject(ctx->context);
    for (size_t i = 0; i < sizeof(js_sys_window_funcs) / sizeof(js_sys_window_funcs[0]); i++)
    {
        js_graphic_register_function(ctx->context, window_obj, &js_sys_window_funcs[i], graphic_ctx);
    }
    JS_SetPropertyStr(ctx->context, sys_obj, "window", window_obj);

    JSValue animation_obj = JS_NewObject(ctx->context);
    for (size_t i = 0; i < sizeof(js_sys_animation_funcs) / sizeof(js_sys_animation_funcs[0]); i++)
    {
        js_graphic_register_function(ctx->context, animation_obj, &js_sys_animation_funcs[i], graphic_ctx);
    }
    JS_SetPropertyStr(ctx->context, sys_obj, "animation", animation_obj);

    JS_FreeValue(ctx->context, sys_obj);
    JS_FreeValue(ctx->context, global);

    return graphic_ctx;
}

void js_graphic_set_frame_context(JSGraphicContext *graphic_ctx, SkiaCanvas *canvas,
                                  InputState *input, Window *window, int width, int height,
                                  float display_density)
{
    if (!graphic_ctx)
        return;

    graphic_ctx->drawingDesk.canvas = canvas;
    graphic_ctx->input = input;
    graphic_ctx->window = window;
    graphic_ctx->width = width;
    graphic_ctx->height = height;
    graphic_ctx->display_density = display_density > 0.0f ? display_density : 1.0f;
}

void js_graphic_destroy(JSGraphicContext *ctx)
{
    if (ctx->input)
        input_text_stop(ctx->input);

    if (ctx->has_animation_callback)
    {
        JS_FreeValue(ctx->context, ctx->animation_callback);
    }

    for (int i = 0; i < ctx->path_count; i++)
    {
        skia_path_destroy(ctx->paths[i]);
    }
    free(ctx->paths);

    for (int i = 0; i < ctx->svg_count; i++)
    {
        if (ctx->svgs[i])
        {
            skia_svg_destroy(ctx->svgs[i]);
        }
    }
    free(ctx->svgs);

    for (int i = 0; i < ctx->canvas_texture_count; i++)
    {
        if (ctx->canvas_textures[i])
            window_canvas_texture_destroy(ctx->canvas_textures[i]);
    }
    free(ctx->canvas_textures);

    for (int i = 0; i < ctx->font_count; i++)
    {
        skia_font_destroy(ctx->fonts[i]);
        free(ctx->font_names[i]);
    }
    free(ctx->fonts);
    free(ctx->font_names);

    skia_paint_destroy(ctx->drawingDesk.active_paint);
}

bool js_graphic_call_animation(JSGraphicContext *graphic_ctx, double timestamp)
{
    if (!graphic_ctx->has_animation_callback)
        return false;

    JSValue callback = graphic_ctx->animation_callback;
    JSValue global = JS_GetGlobalObject(graphic_ctx->context);
    JSValue args[1] = {JS_NewFloat64(graphic_ctx->context, timestamp)};

    JSValue result = JS_Call(graphic_ctx->context, callback, global, 1, args);

    JS_FreeValue(graphic_ctx->context, args[0]);
    JS_FreeValue(graphic_ctx->context, global);

    if (JS_IsException(result))
    {
        js_dump_error(graphic_ctx->context);
        JS_FreeValue(graphic_ctx->context, result);
        return false;
    }

    JS_FreeValue(graphic_ctx->context, result);
    return true;
}

bool js_graphic_has_animation(JSGraphicContext *graphic_ctx)
{
    return graphic_ctx->has_animation_callback;
}