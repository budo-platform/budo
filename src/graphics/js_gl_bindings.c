#include "graphics/js_canvas_bindings.h"
#include "graphics/js_gl_bindings.h"
#include "core/window.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#endif

static JSValue js_throw_window_error(JSContext *ctx, JSGraphicContext *graphic_ctx)
{
    const char *message = "OpenGL operation failed";
    if (graphic_ctx->window &&
        window_gl_get_error(graphic_ctx->window))
        message = window_gl_get_error(graphic_ctx->window);
    return JS_ThrowInternalError(ctx, "%s", message);
}

static const uint8_t *js_get_buffer_bytes(JSContext *ctx, JSValue val,
                                          size_t *byte_count, JSValue *owner)
{
    size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
    size_t array_buffer_size = 0;
    uint8_t *raw;
    *owner = JS_UNDEFINED;
    if (JS_IsArrayBuffer(val))
    {
        raw = JS_GetArrayBuffer(ctx, &array_buffer_size, val);
        if (raw)
        {
            *byte_count = array_buffer_size;
            return raw;
        }
    }
    if (JS_IsObject(val))
    {
        JSValue array_buffer = JS_GetTypedArrayBuffer(
            ctx, val, &byte_offset, &byte_length, &bytes_per_element);
        if (!JS_IsException(array_buffer))
        {
            raw = JS_GetArrayBuffer(ctx, &array_buffer_size, array_buffer);
            if (raw && byte_offset + byte_length <= array_buffer_size)
            {
                *owner = array_buffer;
                *byte_count = byte_length;
                return raw + byte_offset;
            }
            JS_FreeValue(ctx, array_buffer);
        }
    }
    return NULL;
}

static int js_get_canvas_texture_id_from_value(JSContext *ctx, JSValue value)
{
    int id = 0;
    if (JS_IsNumber(value))
        JS_ToInt32(ctx, &id, value);
    else if (JS_IsObject(value))
    {
        JSValue property = JS_GetPropertyStr(ctx, value, "__canvasTextureId");
        if (!JS_IsUndefined(property))
            JS_ToInt32(ctx, &id, property);
        JS_FreeValue(ctx, property);
    }
    return id;
}

static CanvasTexture *js_get_canvas_texture_from_value(JSContext *ctx, JSGraphicContext *graphic_ctx, JSValue value)
{
    int id = js_get_canvas_texture_id_from_value(ctx, value);
    if (!graphic_ctx || id <= 0 || id > graphic_ctx->canvas_texture_count)
        return NULL;
    return graphic_ctx->canvas_textures[id - 1];
}

static JSValue js_gl_create_program(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *vertex_path;
    const char *fragment_path;
    int program_id;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 2)
        return JS_EXCEPTION;

    vertex_path = JS_ToCString(ctx, argv[0]);
    fragment_path = JS_ToCString(ctx, argv[1]);
    if (!vertex_path || !fragment_path)
    {
        if (vertex_path)
            JS_FreeCString(ctx, vertex_path);
        if (fragment_path)
            JS_FreeCString(ctx, fragment_path);
        return JS_EXCEPTION;
    }

    program_id = window_gl_create_program(graphic_ctx->window, vertex_path, fragment_path);
    JS_FreeCString(ctx, vertex_path);
    JS_FreeCString(ctx, fragment_path);

    if (program_id < 0)
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_NewInt32(ctx, program_id);
}

static JSValue js_gl_create_program_from_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    size_t vertex_len = 0, fragment_len = 0;
    JSValue vertex_ref = JS_UNDEFINED, fragment_ref = JS_UNDEFINED;
    const uint8_t *vertex_source;
    const uint8_t *fragment_source;
    int program_id;
    (void)this_val;

    if (argc < 2)
        return JS_EXCEPTION;

    vertex_source = js_get_buffer_bytes(ctx, argv[0], &vertex_len, &vertex_ref);
    fragment_source = js_get_buffer_bytes(ctx, argv[1], &fragment_len, &fragment_ref);
    if (!vertex_source || !fragment_source)
    {
        if (!JS_IsUndefined(vertex_ref))
            JS_FreeValue(ctx, vertex_ref);
        if (!JS_IsUndefined(fragment_ref))
            JS_FreeValue(ctx, fragment_ref);
        return JS_ThrowTypeError(ctx, "createProgramFromBuffer requires vertex and fragment ArrayBuffers");
    }

    program_id = window_gl_create_program_from_source(graphic_ctx->window,
                                                      (const char *)vertex_source, vertex_len,
                                                      (const char *)fragment_source, fragment_len);
    if (!JS_IsUndefined(vertex_ref))
        JS_FreeValue(ctx, vertex_ref);
    if (!JS_IsUndefined(fragment_ref))
        JS_FreeValue(ctx, fragment_ref);

    if (program_id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, program_id);
}

static JSValue js_gl_use_program(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 1)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    if (program_id <= 0)
        return JS_ThrowReferenceError(ctx, "Invalid shader program");
    if (!window_gl_use_program(graphic_ctx->window, program_id))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_bind_screen(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    skia_canvas_flush(graphic_ctx->drawingDesk.canvas);

    if (!window_gl_bind_screen(graphic_ctx->window))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_bind_render_target_immediate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int target_id = 0;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
        JS_ToInt32(ctx, &target_id, argv[0]);
    if (!window_gl_bind_render_target_immediate(graphic_ctx->window, target_id))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_destroy_program(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 1)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    if (!window_gl_destroy_program(graphic_ctx->window, program_id))
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_UNDEFINED;
}

static JSValue js_gl_draw_fullscreen(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 1)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    if (!window_gl_draw_fullscreen(graphic_ctx->window, program_id))
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_UNDEFINED;
}

static JSValue js_gl_set_uniform_1i(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    int value;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 3)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    JS_ToInt32(ctx, &value, argv[2]);
    if (!name)
        return JS_EXCEPTION;

    if (!window_gl_set_uniform_1i(graphic_ctx->window, program_id, name, value))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_set_uniform_1f(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    double value;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 3)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    JS_ToFloat64(ctx, &value, argv[2]);
    if (!name)
        return JS_EXCEPTION;

    if (!window_gl_set_uniform_1f(graphic_ctx->window, program_id, name, (float)value))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_set_uniform_2f(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    double v0, v1;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 4)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    JS_ToFloat64(ctx, &v0, argv[2]);
    JS_ToFloat64(ctx, &v1, argv[3]);
    if (!name)
        return JS_EXCEPTION;

    if (!window_gl_set_uniform_2f(graphic_ctx->window, program_id, name, (float)v0, (float)v1))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_set_uniform_3f(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    double v0, v1, v2;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 5)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    JS_ToFloat64(ctx, &v0, argv[2]);
    JS_ToFloat64(ctx, &v1, argv[3]);
    JS_ToFloat64(ctx, &v2, argv[4]);
    if (!name)
        return JS_EXCEPTION;

    if (!window_gl_set_uniform_3f(graphic_ctx->window, program_id, name, (float)v0, (float)v1, (float)v2))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_set_uniform_4f(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    double v0, v1, v2, v3;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 6)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    JS_ToFloat64(ctx, &v0, argv[2]);
    JS_ToFloat64(ctx, &v1, argv[3]);
    JS_ToFloat64(ctx, &v2, argv[4]);
    JS_ToFloat64(ctx, &v3, argv[5]);
    if (!name)
        return JS_EXCEPTION;

    if (!window_gl_set_uniform_4f(graphic_ctx->window, program_id, name, (float)v0, (float)v1, (float)v2, (float)v3))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_get_last_error(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    const char *message;

    message = window_gl_get_error(graphic_ctx->window);
    return JS_NewString(ctx, message ? message : "");
}

static JSValue js_gl_get_project_dir(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *dir;

    (void)argc;
    (void)argv;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    dir = window_get_project_dir(graphic_ctx->window);
    return JS_NewString(ctx, dir ? dir : "");
}

static JSValue js_gl_create_render_target(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int width, height, target_id;
    bool depth = false;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 2)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &width, argv[0]);
    JS_ToInt32(ctx, &height, argv[1]);
    if (argc >= 3)
        depth = JS_ToBool(ctx, argv[2]);

    target_id = window_gl_create_render_target(graphic_ctx->window, width, height, depth);
    if (target_id < 0)
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_NewInt32(ctx, target_id);
}

static JSValue js_gl_destroy_render_target(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int target_id;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 1)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &target_id, argv[0]);
    if (!window_gl_destroy_render_target(graphic_ctx->window, target_id))
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_UNDEFINED;
}

static JSValue js_gl_resize_render_target(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int target_id, width, height;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 3)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &target_id, argv[0]);
    JS_ToInt32(ctx, &width, argv[1]);
    JS_ToInt32(ctx, &height, argv[2]);
    if (!window_gl_resize_render_target(graphic_ctx->window, target_id, width, height))
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_UNDEFINED;
}

static JSValue js_gl_draw_region(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, target_id = -1;
    double x, y, w, h;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 5)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);
    JS_ToFloat64(ctx, &w, argv[3]);
    JS_ToFloat64(ctx, &h, argv[4]);
    if (argc >= 6 && !JS_IsUndefined(argv[5]))
        JS_ToInt32(ctx, &target_id, argv[5]);

    if (!window_gl_draw_region(graphic_ctx->window, program_id,
                               (float)x, (float)y, (float)w, (float)h, target_id))
        return js_throw_window_error(ctx, graphic_ctx);

    return JS_UNDEFINED;
}

static JSValue js_gl_bind_texture(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, render_target_id, texture_unit;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 4)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    JS_ToInt32(ctx, &render_target_id, argv[2]);
    JS_ToInt32(ctx, &texture_unit, argv[3]);
    if (!name)
        return JS_EXCEPTION;

    if (!window_gl_bind_texture(graphic_ctx->window, program_id, name,
                                render_target_id, texture_unit))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_bind_canvas_texture(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, texture_unit;
    const char *name;
    CanvasTexture *canvas_texture;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 4)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    canvas_texture = js_get_canvas_texture_from_value(ctx, graphic_ctx, argv[2]);
    JS_ToInt32(ctx, &texture_unit, argv[3]);
    if (!canvas_texture)
    {
        JS_FreeCString(ctx, name);
        return JS_ThrowReferenceError(ctx, "Invalid or destroyed CanvasTexture");
    }

    if (!window_gl_bind_canvas_texture(graphic_ctx->window, program_id, name,
                                       canvas_texture, texture_unit))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static const uint8_t *js_gl_get_buffer_bytes(JSContext *ctx, JSValue val,
                                             size_t *out_byte_count,
                                             size_t *out_bytes_per_element,
                                             JSValue *out_ab_ref)
{
    *out_ab_ref = JS_UNDEFINED;
    *out_byte_count = 0;
    if (out_bytes_per_element)
        *out_bytes_per_element = 1;

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
                if (out_bytes_per_element)
                    *out_bytes_per_element = bpe;
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

static int js_to_buffer_target(JSContext *ctx, JSValue v, WindowGLBufferTarget *out)
{
    const char *s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "index") == 0 || strcmp(s, "element") == 0)
        *out = WINDOW_GL_BUFFER_INDEX;
    else
        *out = WINDOW_GL_BUFFER_VERTEX;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_buffer_usage(JSContext *ctx, JSValue v, WindowGLBufferUsage *out)
{
    const char *s;
    *out = WINDOW_GL_USAGE_STATIC;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 1;
    if (strcmp(s, "dynamic") == 0)
        *out = WINDOW_GL_USAGE_DYNAMIC;
    else if (strcmp(s, "stream") == 0)
        *out = WINDOW_GL_USAGE_STREAM;
    else
        *out = WINDOW_GL_USAGE_STATIC;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_attr_type(JSContext *ctx, JSValue v, WindowGLAttrType *out)
{
    const char *s;
    *out = WINDOW_GL_ATTR_FLOAT;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "float") == 0)
        *out = WINDOW_GL_ATTR_FLOAT;
    else if (strcmp(s, "byte") == 0)
        *out = WINDOW_GL_ATTR_BYTE;
    else if (strcmp(s, "ubyte") == 0)
        *out = WINDOW_GL_ATTR_UBYTE;
    else if (strcmp(s, "short") == 0)
        *out = WINDOW_GL_ATTR_SHORT;
    else if (strcmp(s, "ushort") == 0)
        *out = WINDOW_GL_ATTR_USHORT;
    else if (strcmp(s, "int") == 0)
        *out = WINDOW_GL_ATTR_INT;
    else if (strcmp(s, "uint") == 0)
        *out = WINDOW_GL_ATTR_UINT;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_index_type(JSContext *ctx, JSValue v, WindowGLIndexType *out)
{
    const char *s;
    *out = WINDOW_GL_INDEX_U16;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "u32") == 0 || strcmp(s, "uint32") == 0)
        *out = WINDOW_GL_INDEX_U32;
    else
        *out = WINDOW_GL_INDEX_U16;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_primitive(JSContext *ctx, JSValue v, WindowGLPrimitive *out)
{
    const char *s;
    *out = WINDOW_GL_PRIM_TRIANGLES;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "triangles") == 0)
        *out = WINDOW_GL_PRIM_TRIANGLES;
    else if (strcmp(s, "triangle_strip") == 0)
        *out = WINDOW_GL_PRIM_TRIANGLE_STRIP;
    else if (strcmp(s, "triangle_fan") == 0)
        *out = WINDOW_GL_PRIM_TRIANGLE_FAN;
    else if (strcmp(s, "lines") == 0)
        *out = WINDOW_GL_PRIM_LINES;
    else if (strcmp(s, "line_strip") == 0)
        *out = WINDOW_GL_PRIM_LINE_STRIP;
    else if (strcmp(s, "points") == 0)
        *out = WINDOW_GL_PRIM_POINTS;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_cull(JSContext *ctx, JSValue v, WindowGLCullMode *out)
{
    const char *s;
    *out = WINDOW_GL_CULL_NONE;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "back") == 0)
        *out = WINDOW_GL_CULL_BACK;
    else if (strcmp(s, "front") == 0)
        *out = WINDOW_GL_CULL_FRONT;
    else
        *out = WINDOW_GL_CULL_NONE;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_blend(JSContext *ctx, JSValue v, WindowGLBlendMode *out)
{
    const char *s;
    *out = WINDOW_GL_BLEND_ALPHA;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "none") == 0)
        *out = WINDOW_GL_BLEND_NONE;
    else if (strcmp(s, "add") == 0)
        *out = WINDOW_GL_BLEND_ADD;
    else if (strcmp(s, "premult") == 0)
        *out = WINDOW_GL_BLEND_PREMULT;
    else
        *out = WINDOW_GL_BLEND_ALPHA;
    JS_FreeCString(ctx, s);
    return 1;
}

static int js_to_tex_format(JSContext *ctx, JSValue v, WindowGLTexFormat *out)
{
    const char *s;
    *out = WINDOW_GL_TEX_RGBA8;
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return 1;
    s = JS_ToCString(ctx, v);
    if (!s)
        return 0;
    if (strcmp(s, "rgb") == 0 || strcmp(s, "rgb8") == 0)
        *out = WINDOW_GL_TEX_RGB8;
    else if (strcmp(s, "r") == 0 || strcmp(s, "r8") == 0)
        *out = WINDOW_GL_TEX_R8;
    else
        *out = WINDOW_GL_TEX_RGBA8;
    JS_FreeCString(ctx, s);
    return 1;
}

static JSValue js_gl_create_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id;
    WindowGLBufferTarget target = WINDOW_GL_BUFFER_VERTEX;
    WindowGLBufferUsage usage = WINDOW_GL_USAGE_STATIC;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc >= 1 && !js_to_buffer_target(ctx, argv[0], &target))
        return JS_EXCEPTION;

    id = window_gl_create_buffer(graphic_ctx->window);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);

    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
    {
        size_t bytes = 0;
        JSValue ab_ref = JS_UNDEFINED;
        const uint8_t *data = js_gl_get_buffer_bytes(ctx, argv[1], &bytes, NULL, &ab_ref);
        if (!data)
        {
            window_gl_destroy_buffer(graphic_ctx->window, id);
            if (!JS_IsUndefined(ab_ref))
                JS_FreeValue(ctx, ab_ref);
            return JS_ThrowTypeError(ctx, "createBuffer: data must be a TypedArray or ArrayBuffer");
        }
        if (argc >= 3)
            js_to_buffer_usage(ctx, argv[2], &usage);
        if (!window_gl_buffer_data(graphic_ctx->window, id, target, data, bytes, usage))
        {
            window_gl_destroy_buffer(graphic_ctx->window, id);
            if (!JS_IsUndefined(ab_ref))
                JS_FreeValue(ctx, ab_ref);
            return js_throw_window_error(ctx, graphic_ctx);
        }
        if (!JS_IsUndefined(ab_ref))
            JS_FreeValue(ctx, ab_ref);
    }

    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_update_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id;
    int32_t offset = 0;
    size_t bytes = 0;
    JSValue ab_ref = JS_UNDEFINED;
    const uint8_t *data;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 2)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &id, argv[0]);
    if (argc >= 3)
        JS_ToInt32(ctx, &offset, argv[2]);
    data = js_gl_get_buffer_bytes(ctx, argv[1], &bytes, NULL, &ab_ref);
    if (!data)
        return JS_ThrowTypeError(ctx, "updateBuffer: data must be a TypedArray or ArrayBuffer");
    if (!window_gl_buffer_sub_data(graphic_ctx->window, id, (size_t)offset, data, bytes))
    {
        if (!JS_IsUndefined(ab_ref))
            JS_FreeValue(ctx, ab_ref);
        return js_throw_window_error(ctx, graphic_ctx);
    }
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    return JS_UNDEFINED;
}

static JSValue js_gl_destroy_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &id, argv[0]);
    if (!window_gl_destroy_buffer(graphic_ctx->window, id))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_create_texture_2d(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int width, height, id;
    WindowGLTexFormat format = WINDOW_GL_TEX_RGBA8;
    const uint8_t *pixels = NULL;
    size_t bytes = 0;
    JSValue ab_ref = JS_UNDEFINED;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 2)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &width, argv[0]);
    JS_ToInt32(ctx, &height, argv[1]);
    if (argc >= 3 && !js_to_tex_format(ctx, argv[2], &format))
        return JS_EXCEPTION;
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
    {
        pixels = js_gl_get_buffer_bytes(ctx, argv[3], &bytes, NULL, &ab_ref);
        if (!pixels)
            return JS_ThrowTypeError(ctx, "createTexture2D: pixels must be a TypedArray or ArrayBuffer");
    }
    id = window_gl_create_texture_2d(graphic_ctx->window, width, height, format, pixels);
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_load_texture_2d(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *path;
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    id = window_gl_create_texture_2d_from_file(graphic_ctx->window, path);
    JS_FreeCString(ctx, path);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_load_texture_2d_from_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    size_t byte_count = 0;
    JSValue ab_ref = JS_UNDEFINED;
    const uint8_t *data;
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    data = js_get_buffer_bytes(ctx, argv[0], &byte_count, &ab_ref);
    if (!data)
        return JS_ThrowTypeError(ctx, "loadTexture2DFromBuffer requires an ArrayBuffer or TypedArray");
    id = window_gl_create_texture_2d_from_buffer(graphic_ctx->window, data, byte_count);
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_load_texture_cube(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const char *paths[6] = {0};
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    if (!BUDO_JS_IS_ARRAY(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "loadTextureCube requires an array of six paths");
    for (uint32_t i = 0; i < 6; ++i)
    {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        paths[i] = JS_ToCString(ctx, item);
        JS_FreeValue(ctx, item);
        if (!paths[i])
        {
            for (uint32_t j = 0; j < i; ++j)
                JS_FreeCString(ctx, paths[j]);
            return JS_ThrowTypeError(ctx, "loadTextureCube requires six string paths");
        }
    }
    id = window_gl_create_texture_cube_from_files(graphic_ctx->window, paths);
    for (uint32_t i = 0; i < 6; ++i)
        JS_FreeCString(ctx, paths[i]);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_load_texture_cube_from_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    const uint8_t *data[6] = {0};
    size_t byte_counts[6] = {0};
    JSValue refs[6] = {JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    if (!BUDO_JS_IS_ARRAY(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "loadTextureCubeFromBuffer requires an array of six buffers");
    for (uint32_t i = 0; i < 6; ++i)
    {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        data[i] = js_get_buffer_bytes(ctx, item, &byte_counts[i], &refs[i]);
        JS_FreeValue(ctx, item);
        if (!data[i])
        {
            for (uint32_t j = 0; j <= i; ++j)
                if (!JS_IsUndefined(refs[j]))
                    JS_FreeValue(ctx, refs[j]);
            return JS_ThrowTypeError(ctx, "loadTextureCubeFromBuffer requires six ArrayBuffers or TypedArrays");
        }
    }
    id = window_gl_create_texture_cube_from_buffers(graphic_ctx->window, data, byte_counts);
    for (uint32_t i = 0; i < 6; ++i)
        if (!JS_IsUndefined(refs[i]))
            JS_FreeValue(ctx, refs[i]);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_update_texture_2d(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id, x, y, w, h;
    size_t bytes = 0;
    JSValue ab_ref = JS_UNDEFINED;
    const uint8_t *pixels;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 6)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToInt32(ctx, &x, argv[1]);
    JS_ToInt32(ctx, &y, argv[2]);
    JS_ToInt32(ctx, &w, argv[3]);
    JS_ToInt32(ctx, &h, argv[4]);
    pixels = js_gl_get_buffer_bytes(ctx, argv[5], &bytes, NULL, &ab_ref);
    if (!pixels)
        return JS_ThrowTypeError(ctx, "updateTexture2D: pixels must be a TypedArray or ArrayBuffer");
    if (!window_gl_update_texture_2d(graphic_ctx->window, id, x, y, w, h, pixels))
    {
        if (!JS_IsUndefined(ab_ref))
            JS_FreeValue(ctx, ab_ref);
        return js_throw_window_error(ctx, graphic_ctx);
    }
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    return JS_UNDEFINED;
}

static JSValue js_gl_destroy_texture(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &id, argv[0]);
    if (!window_gl_destroy_texture(graphic_ctx->window, id))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_create_vertex_layout(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id;
    (void)this_val;
    (void)argc;
    (void)argv;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    id = window_gl_create_vertex_layout(graphic_ctx->window);
    if (id < 0)
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_gl_destroy_vertex_layout(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int id;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &id, argv[0]);
    if (!window_gl_destroy_vertex_layout(graphic_ctx->window, id))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_set_attribute(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int layout, location, buffer, size;
    int32_t stride = 0, offset = 0, divisor = 0;
    int normalized = 0;
    WindowGLAttrType type = WINDOW_GL_ATTR_FLOAT;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 4)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &layout, argv[0]);
    JS_ToInt32(ctx, &location, argv[1]);
    JS_ToInt32(ctx, &buffer, argv[2]);
    JS_ToInt32(ctx, &size, argv[3]);
    if (argc >= 5 && !js_to_attr_type(ctx, argv[4], &type))
        return JS_EXCEPTION;
    if (argc >= 6)
        normalized = JS_ToBool(ctx, argv[5]);
    if (argc >= 7)
        JS_ToInt32(ctx, &stride, argv[6]);
    if (argc >= 8)
        JS_ToInt32(ctx, &offset, argv[7]);
    if (argc >= 9)
        JS_ToInt32(ctx, &divisor, argv[8]);
    if (!window_gl_set_attribute(graphic_ctx->window, layout, location, buffer,
                                 size, type, normalized != 0, stride, offset, divisor))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_set_index_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int layout, buffer;
    WindowGLIndexType type = WINDOW_GL_INDEX_U16;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 2)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &layout, argv[0]);
    JS_ToInt32(ctx, &buffer, argv[1]);
    if (argc >= 3 && !js_to_index_type(ctx, argv[2], &type))
        return JS_EXCEPTION;
    if (!window_gl_set_index_buffer(graphic_ctx->window, layout, buffer, type))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static float *js_gl_floats_from_value(JSContext *ctx, JSValue v, int *out_count)
{
    JSValue ab_ref = JS_UNDEFINED;
    size_t bytes = 0, bpe = 0;
    const uint8_t *raw = js_gl_get_buffer_bytes(ctx, v, &bytes, &bpe, &ab_ref);
    float *out;
    int count;
    if (!raw)
        return NULL;
    if (bpe != 4 && bpe != 1)
    {
        if (!JS_IsUndefined(ab_ref))
            JS_FreeValue(ctx, ab_ref);
        return NULL;
    }
    count = (int)(bytes / 4);
    out = (float *)malloc((size_t)count * sizeof(float));
    if (!out)
    {
        if (!JS_IsUndefined(ab_ref))
            JS_FreeValue(ctx, ab_ref);
        return NULL;
    }
    memcpy(out, raw, (size_t)count * sizeof(float));
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    *out_count = count;
    return out;
}

static int *js_gl_ints_from_value(JSContext *ctx, JSValue v, int *out_count)
{
    JSValue ab_ref = JS_UNDEFINED;
    size_t bytes = 0, bpe = 0;
    const uint8_t *raw = js_gl_get_buffer_bytes(ctx, v, &bytes, &bpe, &ab_ref);
    int *out;
    int count;
    if (!raw)
        return NULL;
    count = (int)(bytes / 4);
    out = (int *)malloc((size_t)count * sizeof(int));
    if (!out)
    {
        if (!JS_IsUndefined(ab_ref))
            JS_FreeValue(ctx, ab_ref);
        return NULL;
    }
    memcpy(out, raw, (size_t)count * sizeof(int));
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);
    *out_count = count;
    return out;
}

#define DEFINE_UNIFORM_FV_BINDING(jsname, c_func, components_per_elem)                                              \
    static JSValue jsname(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data) \
    {                                                                                                               \
        int program_id;                                                                                             \
        const char *name;                                                                                           \
        float *values;                                                                                              \
        int total_floats;                                                                                           \
        int count;                                                                                                  \
        (void)this_val;                                                                                             \
        JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);                                         \
        if (!graphic_ctx)                                                                                           \
            return JS_ThrowInternalError(ctx, "No active graphic");                                                 \
        if (argc < 3)                                                                                               \
            return JS_EXCEPTION;                                                                                    \
        JS_ToInt32(ctx, &program_id, argv[0]);                                                                      \
        name = JS_ToCString(ctx, argv[1]);                                                                          \
        if (!name)                                                                                                  \
            return JS_EXCEPTION;                                                                                    \
        values = js_gl_floats_from_value(ctx, argv[2], &total_floats);                                              \
        if (!values)                                                                                                \
        {                                                                                                           \
            JS_FreeCString(ctx, name);                                                                              \
            return JS_ThrowTypeError(ctx, #jsname ": values must be a Float32Array");                               \
        }                                                                                                           \
        count = total_floats / (components_per_elem);                                                               \
        if (count < 1)                                                                                              \
            count = 1;                                                                                              \
        if (!c_func(graphic_ctx->window, program_id, name, values, count))                                          \
        {                                                                                                           \
            free(values);                                                                                           \
            JS_FreeCString(ctx, name);                                                                              \
            return js_throw_window_error(ctx, graphic_ctx);                                                         \
        }                                                                                                           \
        free(values);                                                                                               \
        JS_FreeCString(ctx, name);                                                                                  \
        return JS_UNDEFINED;                                                                                        \
    }

DEFINE_UNIFORM_FV_BINDING(js_gl_set_uniform_matrix3, window_gl_set_uniform_matrix3fv, 9)
DEFINE_UNIFORM_FV_BINDING(js_gl_set_uniform_matrix4, window_gl_set_uniform_matrix4fv, 16)
DEFINE_UNIFORM_FV_BINDING(js_gl_set_uniform_1fv, window_gl_set_uniform_1fv, 1)
DEFINE_UNIFORM_FV_BINDING(js_gl_set_uniform_2fv, window_gl_set_uniform_2fv, 2)
DEFINE_UNIFORM_FV_BINDING(js_gl_set_uniform_3fv, window_gl_set_uniform_3fv, 3)
DEFINE_UNIFORM_FV_BINDING(js_gl_set_uniform_4fv, window_gl_set_uniform_4fv, 4)

#undef DEFINE_UNIFORM_FV_BINDING

static JSValue js_gl_set_uniform_1iv(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    const char *name;
    int *values;
    int count;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 3)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    values = js_gl_ints_from_value(ctx, argv[2], &count);
    if (!values)
    {
        JS_FreeCString(ctx, name);
        return JS_ThrowTypeError(ctx, "setUniform1iv: values must be an Int32Array");
    }
    if (!window_gl_set_uniform_1iv(graphic_ctx->window, program_id, name, values, count))
    {
        free(values);
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }
    free(values);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_bind_texture_2d(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, texture_id, texture_unit;
    const char *name;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 4)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &texture_id, argv[2]);
    JS_ToInt32(ctx, &texture_unit, argv[3]);
    if (!window_gl_bind_texture_2d(graphic_ctx->window, program_id, name, texture_id, texture_unit))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_bind_texture_cube(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, texture_id, texture_unit;
    const char *name;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 4)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &texture_id, argv[2]);
    JS_ToInt32(ctx, &texture_unit, argv[3]);
    if (!window_gl_bind_texture_cube(graphic_ctx->window, program_id, name, texture_id, texture_unit))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_texture(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    int texture_id, texture_unit;
    const char *name;
    CanvasTexture *canvas_texture;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 4)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    if (program_id <= 0)
        return JS_EXCEPTION;
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &texture_unit, argv[3]);

    canvas_texture = js_get_canvas_texture_from_value(ctx, graphic_ctx, argv[2]);
    if (canvas_texture)
    {
        if (!window_gl_bind_canvas_texture_immediate(graphic_ctx->window, program_id, name,
                                                     canvas_texture, texture_unit))
        {
            JS_FreeCString(ctx, name);
            return js_throw_window_error(ctx, graphic_ctx);
        }
    }
    else
    {
        JS_ToInt32(ctx, &texture_id, argv[2]);
        if (!window_gl_bind_texture_2d_immediate(graphic_ctx->window, program_id, name,
                                                 texture_id, texture_unit))
        {
            JS_FreeCString(ctx, name);
            return js_throw_window_error(ctx, graphic_ctx);
        }
    }

    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_render_target_texture(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    int target_id, texture_unit;
    const char *name;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");

    if (argc < 4)
        return JS_EXCEPTION;

    JS_ToInt32(ctx, &program_id, argv[0]);
    if (program_id < 0)
        return JS_EXCEPTION;
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &target_id, argv[2]);
    JS_ToInt32(ctx, &texture_unit, argv[3]);
    if (!window_gl_bind_texture_immediate(graphic_ctx->window, program_id, name,
                                          target_id, texture_unit))
    {
        JS_FreeCString(ctx, name);
        return js_throw_window_error(ctx, graphic_ctx);
    }
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_gl_draw_mesh(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, layout_id;
    
    int32_t first = 0, count = 0, target_id = 0, instance_count = 1;
    WindowGLPrimitive mode = WINDOW_GL_PRIM_TRIANGLES;
    WindowGLDrawState state;
    JSValue opts;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 3)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    JS_ToInt32(ctx, &layout_id, argv[1]);
    opts = argv[2];

    state.depth_test = true;
    state.depth_write = true;
    state.cull = WINDOW_GL_CULL_NONE;
    state.blend = WINDOW_GL_BLEND_ALPHA;

    if (JS_IsObject(opts))
    {
        JSValue v;
        v = JS_GetPropertyStr(ctx, opts, "count");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &count, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "first");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &first, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "target");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &target_id, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "instanceCount");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &instance_count, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "mode");
        if (!JS_IsUndefined(v))
            js_to_primitive(ctx, v, &mode);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "cull");
        if (!JS_IsUndefined(v))
            js_to_cull(ctx, v, &state.cull);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "blend");
        if (!JS_IsUndefined(v))
            js_to_blend(ctx, v, &state.blend);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "depthTest");
        if (!JS_IsUndefined(v))
            state.depth_test = JS_ToBool(ctx, v) != 0;
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "depthWrite");
        if (!JS_IsUndefined(v))
            state.depth_write = JS_ToBool(ctx, v) != 0;
        JS_FreeValue(ctx, v);
    }

    if (!window_gl_draw_mesh(graphic_ctx->window, program_id, layout_id,
                             mode, first, count, target_id, &state, instance_count))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static uint32_t js_gl_texture_id_from_draw_source(JSContext *ctx, JSGraphicContext *graphic_ctx, JSValue value)
{
    CanvasTexture *canvas_texture = js_get_canvas_texture_from_value(ctx, graphic_ctx, value);
    uint32_t texture_id = 0;

    if (canvas_texture)
    {
        window_canvas_texture_flush(canvas_texture);
        return window_canvas_texture_get_gl_texture(canvas_texture);
    }

    JS_ToUint32(ctx, &texture_id, value);
    return texture_id;
}

static JSValue js_gl_draw_fullscreen_immediate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id;
    uint32_t source_texture = 0;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 1)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    if (program_id <= 0)
        return JS_ThrowReferenceError(ctx, "Invalid shader program");
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
        source_texture = js_gl_texture_id_from_draw_source(ctx, graphic_ctx, argv[1]);
    if (!window_gl_draw_fullscreen_immediate(graphic_ctx->window, program_id, source_texture))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_draw_region_immediate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, target_id = 0;
    double x, y, w, h;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 5)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    if (program_id <= 0)
        return JS_ThrowReferenceError(ctx, "Invalid shader program");
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);
    JS_ToFloat64(ctx, &w, argv[3]);
    JS_ToFloat64(ctx, &h, argv[4]);
    if (argc >= 6 && !JS_IsUndefined(argv[5]) && !JS_IsNull(argv[5]))
        JS_ToInt32(ctx, &target_id, argv[5]);
    if (!window_gl_draw_region_immediate(graphic_ctx->window, program_id,
                                         (float)x, (float)y, (float)w, (float)h,
                                         target_id))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_draw_mesh_immediate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, layout_id;
    int32_t first = 0, count = 0, target_id = 0, instance_count = 1;
    WindowGLPrimitive mode = WINDOW_GL_PRIM_TRIANGLES;
    WindowGLDrawState state;
    JSValue opts;
    (void)this_val;

    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 3)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    if (program_id <= 0)
        return JS_ThrowReferenceError(ctx, "Invalid shader program");
    JS_ToInt32(ctx, &layout_id, argv[1]);
    opts = argv[2];

    state.depth_test = true;
    state.depth_write = true;
    state.cull = WINDOW_GL_CULL_NONE;
    state.blend = WINDOW_GL_BLEND_ALPHA;

    if (JS_IsObject(opts))
    {
        JSValue v;
        v = JS_GetPropertyStr(ctx, opts, "count");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &count, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "first");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &first, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "target");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &target_id, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "instanceCount");
        if (!JS_IsUndefined(v))
            JS_ToInt32(ctx, &instance_count, v);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "mode");
        if (!JS_IsUndefined(v))
            js_to_primitive(ctx, v, &mode);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "cull");
        if (!JS_IsUndefined(v))
            js_to_cull(ctx, v, &state.cull);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "blend");
        if (!JS_IsUndefined(v))
            js_to_blend(ctx, v, &state.blend);
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "depthTest");
        if (!JS_IsUndefined(v))
            state.depth_test = JS_ToBool(ctx, v) != 0;
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, opts, "depthWrite");
        if (!JS_IsUndefined(v))
            state.depth_write = JS_ToBool(ctx, v) != 0;
        JS_FreeValue(ctx, v);
    }

    if (!window_gl_draw_mesh_immediate(graphic_ctx->window, program_id, layout_id,
                                       mode, first, count, target_id, &state, instance_count))
        return js_throw_window_error(ctx, graphic_ctx);
    return JS_UNDEFINED;
}

static JSValue js_gl_get_attrib_location(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic, JSValue *func_data)
{
    int program_id, loc;
    const char *name;
    (void)this_val;
    JSGraphicContext *graphic_ctx = js_graphic_context(ctx, func_data);
    if (!graphic_ctx)
        return JS_ThrowInternalError(ctx, "No active graphic");
    if (argc < 2)
        return JS_EXCEPTION;
    JS_ToInt32(ctx, &program_id, argv[0]);
    name = JS_ToCString(ctx, argv[1]);
    if (!name)
        return JS_EXCEPTION;
    loc = window_gl_get_attrib_location(graphic_ctx->window, program_id, name);
    JS_FreeCString(ctx, name);
    return JS_NewInt32(ctx, loc);
}

static const JsGraphicFunction js_sys_gl_funcs[] = {
    {"createProgram", 2, js_gl_create_program},
    {"createProgramFromBuffer", 2, js_gl_create_program_from_buffer},
    {"destroyProgram", 1, js_gl_destroy_program},
    {"useProgram", 1, js_gl_use_program},
    {"bindScreen", 0, js_gl_bind_screen},
    {"bindRenderTarget", 1, js_gl_bind_render_target_immediate},
    {"drawFullscreen", 1, js_gl_draw_fullscreen},
    {"drawFullscreenImmediate", 2, js_gl_draw_fullscreen_immediate},
    {"setUniform1i", 3, js_gl_set_uniform_1i},
    {"setUniform1f", 3, js_gl_set_uniform_1f},
    {"setUniform2f", 4, js_gl_set_uniform_2f},
    {"setUniform3f", 5, js_gl_set_uniform_3f},
    {"setUniform4f", 6, js_gl_set_uniform_4f},
    {"getLastError", 0, js_gl_get_last_error},
    {"getProjectDir", 0, js_gl_get_project_dir},
    {"createRenderTarget", 2, js_gl_create_render_target},
    {"destroyRenderTarget", 1, js_gl_destroy_render_target},
    {"resizeRenderTarget", 3, js_gl_resize_render_target},
    {"drawRegion", 5, js_gl_draw_region},
    {"bindTexture", 4, js_gl_bind_texture},
    {"bindCanvasTexture", 4, js_gl_bind_canvas_texture},
    {"createBuffer", 3, js_gl_create_buffer},
    {"updateBuffer", 3, js_gl_update_buffer},
    {"destroyBuffer", 1, js_gl_destroy_buffer},
    {"createTexture2D", 4, js_gl_create_texture_2d},
    {"loadTexture2D", 1, js_gl_load_texture_2d},
    {"loadTexture2DFromBuffer", 1, js_gl_load_texture_2d_from_buffer},
    {"loadTextureCube", 1, js_gl_load_texture_cube},
    {"loadTextureCubeFromBuffer", 1, js_gl_load_texture_cube_from_buffer},
    {"updateTexture2D", 6, js_gl_update_texture_2d},
    {"destroyTexture", 1, js_gl_destroy_texture},
    {"createVertexLayout", 0, js_gl_create_vertex_layout},
    {"destroyVertexLayout", 1, js_gl_destroy_vertex_layout},
    {"setAttribute", 9, js_gl_set_attribute},
    {"setIndexBuffer", 3, js_gl_set_index_buffer},
    {"setUniformMatrix3", 3, js_gl_set_uniform_matrix3},
    {"setUniformMatrix4", 3, js_gl_set_uniform_matrix4},
    {"setUniform1iv", 3, js_gl_set_uniform_1iv},
    {"setUniform1fv", 3, js_gl_set_uniform_1fv},
    {"setUniform2fv", 3, js_gl_set_uniform_2fv},
    {"setUniform3fv", 3, js_gl_set_uniform_3fv},
    {"setUniform4fv", 3, js_gl_set_uniform_4fv},
    {"bindTexture2D", 4, js_gl_bind_texture_2d},
    {"bindTextureCube", 4, js_gl_bind_texture_cube},
    {"drawMesh", 3, js_gl_draw_mesh},
    {"drawRegionImmediate", 6, js_gl_draw_region_immediate},
    {"drawMeshImmediate", 3, js_gl_draw_mesh_immediate},
    {"getAttribLocation", 2, js_gl_get_attrib_location},
    {"renderTargetTexture", 4, js_gl_render_target_texture},
    {"texture", 4, js_gl_texture},
};

void js_gl_register(JSContext *context, JSGraphicContext *graphic_ctx, JSValue sys_object)
{
    JSValue obj = JS_NewObject(context);
    for (size_t i = 0; i < sizeof(js_sys_gl_funcs) / sizeof(js_sys_gl_funcs[0]); i++)
    {
        js_graphic_register_function(context, obj, &js_sys_gl_funcs[i], graphic_ctx);
    }
    JS_SetPropertyStr(context, sys_object, "gl", obj);
}