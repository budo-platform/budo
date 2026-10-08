#include "graphics/lua_gl_bindings.h"

#include "graphics/lua_canvas_bindings.h"
#include "core/window.h"

#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lua_canvas_get_context(L) lua_canvas_graphics_context(L)

static int l_gl_create_program(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return luaL_error(L, "createProgram requires vertex and fragment paths");
    const char *vertex_path = luaL_checkstring(L, 1);
    const char *fragment_path = luaL_checkstring(L, 2);
    int program_id = window_gl_create_program(ctx->window, vertex_path, fragment_path);
    if (program_id < 0)
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    lua_pushinteger(L, program_id);
    return 1;
}

static int l_gl_create_program_from_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return luaL_error(L, "createProgramFromBuffer requires vertex and fragment buffers");
    size_t vertex_len = 0;
    size_t fragment_len = 0;
    const char *vertex_source = luaL_checklstring(L, 1, &vertex_len);
    const char *fragment_source = luaL_checklstring(L, 2, &fragment_len);
    int program_id = window_gl_create_program_from_source(ctx->window,
                                                          vertex_source, vertex_len,
                                                          fragment_source, fragment_len);
    if (program_id < 0)
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    lua_pushinteger(L, program_id);
    return 1;
}

static int l_gl_destroy_program(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    window_gl_destroy_program(ctx->window, program_id);
    return 0;
}

static int l_gl_use_program(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    if (!window_gl_use_program(ctx->window, program_id))
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    return 0;
}

static int l_gl_bind_screen(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window)
        return 0;
    if (!window_gl_bind_screen(ctx->window))
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    return 0;
}

static int l_gl_bind_render_target(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window)
        return 0;
    int target_id = lua_isnoneornil(L, 1) ? 0 : (int)luaL_checkinteger(L, 1);
    if (!window_gl_bind_render_target_immediate(ctx->window, target_id))
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    return 0;
}

static int l_gl_draw_fullscreen(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    uint32_t source_texture = lua_isnoneornil(L, 2) ? 0u : (uint32_t)luaL_checkinteger(L, 2);
    if (!window_gl_bind_screen(ctx->window) ||
        !window_gl_draw_fullscreen_immediate(ctx->window, program_id, source_texture))
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    return 0;
}

static int l_gl_set_uniform_1i(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 3)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int value = (int)luaL_checkinteger(L, 3);
    window_gl_set_uniform_1i(ctx->window, program_id, name, value);
    return 0;
}

static int l_gl_set_uniform_1f(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 3)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    float value = (float)luaL_checknumber(L, 3);
    window_gl_set_uniform_1f(ctx->window, program_id, name, value);
    return 0;
}

static int l_gl_set_uniform_2f(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 4)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    float v0 = (float)luaL_checknumber(L, 3);
    float v1 = (float)luaL_checknumber(L, 4);
    window_gl_set_uniform_2f(ctx->window, program_id, name, v0, v1);
    return 0;
}

static int l_gl_set_uniform_3f(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 5)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    float v0 = (float)luaL_checknumber(L, 3);
    float v1 = (float)luaL_checknumber(L, 4);
    float v2 = (float)luaL_checknumber(L, 5);
    window_gl_set_uniform_3f(ctx->window, program_id, name, v0, v1, v2);
    return 0;
}

static int l_gl_set_uniform_4f(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 6)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    float v0 = (float)luaL_checknumber(L, 3);
    float v1 = (float)luaL_checknumber(L, 4);
    float v2 = (float)luaL_checknumber(L, 5);
    float v3 = (float)luaL_checknumber(L, 6);
    window_gl_set_uniform_4f(ctx->window, program_id, name, v0, v1, v2, v3);
    return 0;
}

static int l_gl_get_last_error(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window)
    {
        lua_pushstring(L, "");
        return 1;
    }
    const char *message = window_gl_get_error(ctx->window);
    lua_pushstring(L, message ? message : "");
    return 1;
}

static int l_gl_get_project_dir(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window)
    {
        lua_pushstring(L, "");
        return 1;
    }
    const char *dir = window_get_project_dir(ctx->window);
    lua_pushstring(L, dir ? dir : "");
    return 1;
}

static int l_gl_create_render_target(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return luaL_error(L, "createRenderTarget requires width and height");
    int width = (int)luaL_checkinteger(L, 1);
    int height = (int)luaL_checkinteger(L, 2);
    bool depth = false;
    if (lua_gettop(L) >= 3)
        depth = lua_toboolean(L, 3);
    int target_id = window_gl_create_render_target(ctx->window, width, height, depth);
    if (target_id < 0)
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    lua_pushinteger(L, target_id);
    return 1;
}

static int l_gl_destroy_render_target(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int target_id = (int)luaL_checkinteger(L, 1);
    window_gl_destroy_render_target(ctx->window, target_id);
    return 0;
}

static int l_gl_resize_render_target(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 3)
        return 0;
    int target_id = (int)luaL_checkinteger(L, 1);
    int width = (int)luaL_checkinteger(L, 2);
    int height = (int)luaL_checkinteger(L, 3);
    window_gl_resize_render_target(ctx->window, target_id, width, height);
    return 0;
}

static int l_gl_draw_region(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 5)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    int target_id = -1;
    if (lua_gettop(L) >= 6 && !lua_isnil(L, 6))
        target_id = (int)luaL_checkinteger(L, 6);
    if (!window_gl_draw_region_immediate(ctx->window, program_id, x, y, w, h, target_id))
    {
        const char *err = window_gl_get_error(ctx->window);
        return luaL_error(L, "OpenGL error: %s", err ? err : "unknown");
    }
    return 0;
}

static int l_gl_bind_texture(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 4)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int render_target_id = (int)luaL_checkinteger(L, 3);
    int texture_unit = (int)luaL_checkinteger(L, 4);
    if (!window_gl_bind_texture_immediate(ctx->window, program_id, name, render_target_id, texture_unit))
        return luaL_error(L, "bindTexture: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int lua_gl_type_size(const char *t)
{
    if (!t)
        return 4;
    if (strcmp(t, "f32") == 0 || strcmp(t, "float") == 0)
        return 4;
    if (strcmp(t, "u32") == 0 || strcmp(t, "i32") == 0)
        return 4;
    if (strcmp(t, "u16") == 0 || strcmp(t, "i16") == 0)
        return 2;
    if (strcmp(t, "u8") == 0 || strcmp(t, "i8") == 0)
        return 1;
    return 4;
}

static void *lua_gl_pack_table(lua_State *L, int idx, const char *type, size_t *out_bytes)
{
    int n = (int)lua_rawlen(L, idx);
    int elem = lua_gl_type_size(type);
    size_t bytes = (size_t)n * (size_t)elem;
    void *buf = malloc(bytes ? bytes : 1);
    if (!buf)
        return NULL;
    for (int i = 0; i < n; ++i)
    {
        lua_geti(L, idx, i + 1);
        double dv = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (!type || strcmp(type, "f32") == 0 || strcmp(type, "float") == 0)
            ((float *)buf)[i] = (float)dv;
        else if (strcmp(type, "u32") == 0)
            ((uint32_t *)buf)[i] = (uint32_t)dv;
        else if (strcmp(type, "i32") == 0)
            ((int32_t *)buf)[i] = (int32_t)dv;
        else if (strcmp(type, "u16") == 0)
            ((uint16_t *)buf)[i] = (uint16_t)dv;
        else if (strcmp(type, "i16") == 0)
            ((int16_t *)buf)[i] = (int16_t)dv;
        else if (strcmp(type, "u8") == 0)
            ((uint8_t *)buf)[i] = (uint8_t)dv;
        else if (strcmp(type, "i8") == 0)
            ((int8_t *)buf)[i] = (int8_t)dv;
        else
            ((float *)buf)[i] = (float)dv;
    }
    *out_bytes = bytes;
    return buf;
}

static const void *lua_gl_get_bytes(lua_State *L, int idx, const char *type,
                                    size_t *out_bytes, void **out_owned)
{
    *out_owned = NULL;
    if (lua_isstring(L, idx) && !lua_isnumber(L, idx))
    {
        size_t len = 0;
        const char *s = lua_tolstring(L, idx, &len);
        *out_bytes = len;
        return s;
    }
    if (lua_istable(L, idx))
    {
        void *p = lua_gl_pack_table(L, idx, type, out_bytes);
        *out_owned = p;
        return p;
    }
    return NULL;
}

static WindowGLBufferTarget lua_to_buffer_target(const char *s)
{
    if (s && (strcmp(s, "index") == 0 || strcmp(s, "element") == 0))
        return WINDOW_GL_BUFFER_INDEX;
    return WINDOW_GL_BUFFER_VERTEX;
}

static WindowGLBufferUsage lua_to_buffer_usage(const char *s)
{
    if (!s)
        return WINDOW_GL_USAGE_STATIC;
    if (strcmp(s, "dynamic") == 0)
        return WINDOW_GL_USAGE_DYNAMIC;
    if (strcmp(s, "stream") == 0)
        return WINDOW_GL_USAGE_STREAM;
    return WINDOW_GL_USAGE_STATIC;
}

static WindowGLAttrType lua_to_attr_type(const char *s)
{
    if (!s)
        return WINDOW_GL_ATTR_FLOAT;
    if (strcmp(s, "float") == 0)
        return WINDOW_GL_ATTR_FLOAT;
    if (strcmp(s, "byte") == 0)
        return WINDOW_GL_ATTR_BYTE;
    if (strcmp(s, "ubyte") == 0)
        return WINDOW_GL_ATTR_UBYTE;
    if (strcmp(s, "short") == 0)
        return WINDOW_GL_ATTR_SHORT;
    if (strcmp(s, "ushort") == 0)
        return WINDOW_GL_ATTR_USHORT;
    if (strcmp(s, "int") == 0)
        return WINDOW_GL_ATTR_INT;
    if (strcmp(s, "uint") == 0)
        return WINDOW_GL_ATTR_UINT;
    return WINDOW_GL_ATTR_FLOAT;
}

static WindowGLIndexType lua_to_index_type(const char *s)
{
    if (s && (strcmp(s, "u32") == 0 || strcmp(s, "uint32") == 0))
        return WINDOW_GL_INDEX_U32;
    return WINDOW_GL_INDEX_U16;
}

static WindowGLPrimitive lua_to_primitive(const char *s)
{
    if (!s)
        return WINDOW_GL_PRIM_TRIANGLES;
    if (strcmp(s, "triangles") == 0)
        return WINDOW_GL_PRIM_TRIANGLES;
    if (strcmp(s, "triangle_strip") == 0)
        return WINDOW_GL_PRIM_TRIANGLE_STRIP;
    if (strcmp(s, "triangle_fan") == 0)
        return WINDOW_GL_PRIM_TRIANGLE_FAN;
    if (strcmp(s, "lines") == 0)
        return WINDOW_GL_PRIM_LINES;
    if (strcmp(s, "line_strip") == 0)
        return WINDOW_GL_PRIM_LINE_STRIP;
    if (strcmp(s, "points") == 0)
        return WINDOW_GL_PRIM_POINTS;
    return WINDOW_GL_PRIM_TRIANGLES;
}

static WindowGLCullMode lua_to_cull(const char *s)
{
    if (!s)
        return WINDOW_GL_CULL_NONE;
    if (strcmp(s, "back") == 0)
        return WINDOW_GL_CULL_BACK;
    if (strcmp(s, "front") == 0)
        return WINDOW_GL_CULL_FRONT;
    return WINDOW_GL_CULL_NONE;
}

static WindowGLBlendMode lua_to_blend(const char *s)
{
    if (!s)
        return WINDOW_GL_BLEND_ALPHA;
    if (strcmp(s, "none") == 0)
        return WINDOW_GL_BLEND_NONE;
    if (strcmp(s, "add") == 0)
        return WINDOW_GL_BLEND_ADD;
    if (strcmp(s, "premult") == 0)
        return WINDOW_GL_BLEND_PREMULT;
    return WINDOW_GL_BLEND_ALPHA;
}

static WindowGLTexFormat lua_to_tex_format(const char *s)
{
    if (!s)
        return WINDOW_GL_TEX_RGBA8;
    if (strcmp(s, "rgb") == 0 || strcmp(s, "rgb8") == 0)
        return WINDOW_GL_TEX_RGB8;
    if (strcmp(s, "r") == 0 || strcmp(s, "r8") == 0)
        return WINDOW_GL_TEX_R8;
    return WINDOW_GL_TEX_RGBA8;
}

static int l_gl_create_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window)
        return 0;
    const char *target_s = lua_isstring(L, 1) ? lua_tostring(L, 1) : "vertex";
    WindowGLBufferTarget target = lua_to_buffer_target(target_s);
    int id = window_gl_create_buffer(ctx->window);
    if (id < 0)
        return luaL_error(L, "createBuffer: %s",
                          window_gl_get_error(ctx->window));

    if (!lua_isnoneornil(L, 2))
    {
        const char *type_s = lua_isstring(L, 3) ? lua_tostring(L, 3)
                                                : (target == WINDOW_GL_BUFFER_INDEX ? "u16" : "f32");
        const char *usage_s = lua_isstring(L, 4) ? lua_tostring(L, 4) : NULL;
        WindowGLBufferUsage usage = lua_to_buffer_usage(usage_s);
        size_t bytes = 0;
        void *owned = NULL;
        const void *data = lua_gl_get_bytes(L, 2, type_s, &bytes, &owned);
        if (!data)
        {
            window_gl_destroy_buffer(ctx->window, id);
            return luaL_error(L, "createBuffer: data must be a table or string");
        }
        bool ok = window_gl_buffer_data(ctx->window, id, target,
                                        data, bytes, usage);
        free(owned);
        if (!ok)
        {
            window_gl_destroy_buffer(ctx->window, id);
            return luaL_error(L, "createBuffer: %s",
                              window_gl_get_error(ctx->window));
        }
    }

    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_update_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    const char *type_s = lua_isstring(L, 3) ? lua_tostring(L, 3) : "f32";
    int offset = lua_isnoneornil(L, 4) ? 0 : (int)luaL_checkinteger(L, 4);
    size_t bytes = 0;
    void *owned = NULL;
    const void *data = lua_gl_get_bytes(L, 2, type_s, &bytes, &owned);
    if (!data)
        return luaL_error(L, "updateBuffer: data must be a table or string");
    bool ok = window_gl_buffer_sub_data(ctx->window, id, (size_t)offset, data, bytes);
    free(owned);
    if (!ok)
        return luaL_error(L, "updateBuffer: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_destroy_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    window_gl_destroy_buffer(ctx->window, id);
    return 0;
}

static int l_gl_create_texture_2d(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return 0;
    int w = (int)luaL_checkinteger(L, 1);
    int h = (int)luaL_checkinteger(L, 2);
    const char *fmt_s = lua_isstring(L, 3) ? lua_tostring(L, 3) : NULL;
    WindowGLTexFormat fmt = lua_to_tex_format(fmt_s);
    size_t bytes = 0;
    void *owned = NULL;
    const void *pixels = NULL;
    if (!lua_isnoneornil(L, 4))
    {
        pixels = lua_gl_get_bytes(L, 4, "u8", &bytes, &owned);
        if (!pixels)
            return luaL_error(L, "createTexture2D: pixels must be a table or string");
    }
    int id = window_gl_create_texture_2d(ctx->window, w, h, fmt, pixels);
    free(owned);
    if (id < 0)
        return luaL_error(L, "createTexture2D: %s",
                          window_gl_get_error(ctx->window));
    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_load_texture_2d(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    const char *path = luaL_checkstring(L, 1);
    int id = window_gl_create_texture_2d_from_file(ctx->window, path);
    if (id < 0)
        return luaL_error(L, "loadTexture2D: %s",
                          window_gl_get_error(ctx->window));
    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_load_texture_2d_from_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    size_t len = 0;
    const char *data = luaL_checklstring(L, 1, &len);
    int id = window_gl_create_texture_2d_from_buffer(ctx->window,
                                                     (const uint8_t *)data, len);
    if (id < 0)
        return luaL_error(L, "loadTexture2DFromBuffer: %s",
                          window_gl_get_error(ctx->window));
    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_update_texture_2d(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 6)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int w = (int)luaL_checkinteger(L, 4);
    int h = (int)luaL_checkinteger(L, 5);
    size_t bytes = 0;
    void *owned = NULL;
    const void *pixels = lua_gl_get_bytes(L, 6, "u8", &bytes, &owned);
    if (!pixels)
        return luaL_error(L, "updateTexture2D: pixels must be a table or string");
    bool ok = window_gl_update_texture_2d(ctx->window, id, x, y, w, h, pixels);
    free(owned);
    if (!ok)
        return luaL_error(L, "updateTexture2D: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_destroy_texture(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    window_gl_destroy_texture(ctx->window, id);
    return 0;
}

static int l_gl_load_texture_cube(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    if (!lua_istable(L, 1))
        return luaL_error(L, "loadTextureCube: expected table of 6 file paths");
    const char *paths[6] = {0};
    for (int i = 0; i < 6; ++i)
    {
        lua_geti(L, 1, i + 1);
        paths[i] = lua_tostring(L, -1);
        if (!paths[i])
        {
            lua_pop(L, 1);
            return luaL_error(L, "loadTextureCube: face %d is not a string", i);
        }
        lua_pop(L, 1);
    }
    int id = window_gl_create_texture_cube_from_files(ctx->window, paths);
    if (id < 0)
        return luaL_error(L, "loadTextureCube: %s",
                          window_gl_get_error(ctx->window));
    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_load_texture_cube_from_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    if (!lua_istable(L, 1))
        return luaL_error(L, "loadTextureCubeFromBuffer: expected table of 6 buffers");
    const uint8_t *buffers[6] = {0};
    size_t sizes[6] = {0};
    for (int i = 0; i < 6; ++i)
    {
        lua_geti(L, 1, i + 1);
        buffers[i] = (const uint8_t *)luaL_checklstring(L, -1, &sizes[i]);
        lua_pop(L, 1);
    }
    int id = window_gl_create_texture_cube_from_buffers(ctx->window, buffers, sizes);
    if (id < 0)
        return luaL_error(L, "loadTextureCubeFromBuffer: %s",
                          window_gl_get_error(ctx->window));
    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_create_vertex_layout(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window)
        return 0;
    int id = window_gl_create_vertex_layout(ctx->window);
    if (id < 0)
        return luaL_error(L, "createVertexLayout: %s",
                          window_gl_get_error(ctx->window));
    lua_pushinteger(L, id);
    return 1;
}

static int l_gl_destroy_vertex_layout(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    window_gl_destroy_vertex_layout(ctx->window, id);
    return 0;
}

static int l_gl_set_attribute(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 4)
        return 0;
    int layout = (int)luaL_checkinteger(L, 1);
    int location = (int)luaL_checkinteger(L, 2);
    int buffer = (int)luaL_checkinteger(L, 3);
    int size = (int)luaL_checkinteger(L, 4);
    const char *type_s = lua_isstring(L, 5) ? lua_tostring(L, 5) : NULL;
    WindowGLAttrType type = lua_to_attr_type(type_s);
    bool normalized = lua_isnoneornil(L, 6) ? false : lua_toboolean(L, 6);
    int stride = lua_isnoneornil(L, 7) ? 0 : (int)luaL_checkinteger(L, 7);
    int offset = lua_isnoneornil(L, 8) ? 0 : (int)luaL_checkinteger(L, 8);
    int divisor = lua_isnoneornil(L, 9) ? 0 : (int)luaL_checkinteger(L, 9);
    if (!window_gl_set_attribute(ctx->window, layout, location, buffer,
                                 size, type, normalized, stride, offset, divisor))
        return luaL_error(L, "setAttribute: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_set_index_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return 0;
    int layout = (int)luaL_checkinteger(L, 1);
    int buffer = (int)luaL_checkinteger(L, 2);
    const char *type_s = lua_isstring(L, 3) ? lua_tostring(L, 3) : NULL;
    WindowGLIndexType type = lua_to_index_type(type_s);
    if (!window_gl_set_index_buffer(ctx->window, layout, buffer, type))
        return luaL_error(L, "setIndexBuffer: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

typedef bool (*lua_gl_uniform_fv_fn)(Window *, int, const char *, const float *, int);

static int l_gl_uniform_fv_common(lua_State *L, lua_gl_uniform_fv_fn fn,
                                  int components_per_elem, const char *fname)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 3)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    if (!lua_istable(L, 3))
        return luaL_error(L, "%s: values must be a table of numbers", fname);
    int n = (int)lua_rawlen(L, 3);
    float *values = (float *)malloc((size_t)(n > 0 ? n : 1) * sizeof(float));
    if (!values)
        return luaL_error(L, "%s: out of memory", fname);
    for (int i = 0; i < n; ++i)
    {
        lua_geti(L, 3, i + 1);
        values[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    int count = n / components_per_elem;
    if (count < 1)
        count = 1;
    bool ok = fn(ctx->window, program_id, name, values, count);
    free(values);
    if (!ok)
        return luaL_error(L, "%s: %s", fname,
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_set_uniform_matrix3(lua_State *L)
{
    return l_gl_uniform_fv_common(L, window_gl_set_uniform_matrix3fv, 9, "setUniformMatrix3");
}
static int l_gl_set_uniform_matrix4(lua_State *L)
{
    return l_gl_uniform_fv_common(L, window_gl_set_uniform_matrix4fv, 16, "setUniformMatrix4");
}
static int l_gl_set_uniform_1fv(lua_State *L)
{
    return l_gl_uniform_fv_common(L, window_gl_set_uniform_1fv, 1, "setUniform1fv");
}
static int l_gl_set_uniform_2fv(lua_State *L)
{
    return l_gl_uniform_fv_common(L, window_gl_set_uniform_2fv, 2, "setUniform2fv");
}
static int l_gl_set_uniform_3fv(lua_State *L)
{
    return l_gl_uniform_fv_common(L, window_gl_set_uniform_3fv, 3, "setUniform3fv");
}
static int l_gl_set_uniform_4fv(lua_State *L)
{
    return l_gl_uniform_fv_common(L, window_gl_set_uniform_4fv, 4, "setUniform4fv");
}

static int l_gl_set_uniform_1iv(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 3)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    if (!lua_istable(L, 3))
        return luaL_error(L, "setUniform1iv: values must be a table of integers");
    int n = (int)lua_rawlen(L, 3);
    int *values = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
    if (!values)
        return luaL_error(L, "setUniform1iv: out of memory");
    for (int i = 0; i < n; ++i)
    {
        lua_geti(L, 3, i + 1);
        values[i] = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    bool ok = window_gl_set_uniform_1iv(ctx->window, program_id, name, values, n);
    free(values);
    if (!ok)
        return luaL_error(L, "setUniform1iv: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_bind_texture_2d(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 4)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int texture_id = (int)luaL_checkinteger(L, 3);
    int unit = (int)luaL_checkinteger(L, 4);
    if (!window_gl_bind_texture_2d_immediate(ctx->window, program_id, name, texture_id, unit))
        return luaL_error(L, "bindTexture2D: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_bind_texture_cube(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 4)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int texture_id = (int)luaL_checkinteger(L, 3);
    int unit = (int)luaL_checkinteger(L, 4);
    if (!window_gl_bind_texture_cube_immediate(ctx->window, program_id, name, texture_id, unit))
        return luaL_error(L, "bindTextureCube: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_draw_mesh(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 3)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    int layout_id = (int)luaL_checkinteger(L, 2);

    int first = 0, count = 0, target_id = 0, instance_count = 1;
    WindowGLPrimitive mode = WINDOW_GL_PRIM_TRIANGLES;
    WindowGLDrawState state;
    state.depth_test = true;
    state.depth_write = true;
    state.cull = WINDOW_GL_CULL_NONE;
    state.blend = WINDOW_GL_BLEND_ALPHA;

    if (lua_istable(L, 3))
    {
        lua_getfield(L, 3, "count");
        if (!lua_isnil(L, -1))
            count = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "first");
        if (!lua_isnil(L, -1))
            first = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "target");
        if (!lua_isnil(L, -1))
            target_id = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "instanceCount");
        if (!lua_isnil(L, -1))
            instance_count = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "mode");
        if (lua_isstring(L, -1))
            mode = lua_to_primitive(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 3, "cull");
        if (lua_isstring(L, -1))
            state.cull = lua_to_cull(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 3, "blend");
        if (lua_isstring(L, -1))
            state.blend = lua_to_blend(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 3, "depthTest");
        if (!lua_isnil(L, -1))
            state.depth_test = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "depthWrite");
        if (!lua_isnil(L, -1))
            state.depth_write = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }

    if (!window_gl_draw_mesh_immediate(ctx->window, program_id, layout_id,
                                       mode, first, count, target_id, &state, instance_count))
        return luaL_error(L, "drawMesh: %s",
                          window_gl_get_error(ctx->window));
    return 0;
}

static int l_gl_get_attrib_location(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->window || lua_gettop(L) < 2)
        return 0;
    int program_id = (int)luaL_checkinteger(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int loc = window_gl_get_attrib_location(ctx->window, program_id, name);
    lua_pushinteger(L, loc);
    return 1;
}

static const luaL_Reg gl_funcs[] = {
    {"createProgram", l_gl_create_program},
    {"createProgramFromBuffer", l_gl_create_program_from_buffer},
    {"destroyProgram", l_gl_destroy_program},
    {"useProgram", l_gl_use_program},
    {"bindScreen", l_gl_bind_screen},
    {"bindRenderTarget", l_gl_bind_render_target},
    {"drawFullscreen", l_gl_draw_fullscreen},
    {"setUniform1i", l_gl_set_uniform_1i},
    {"setUniform1f", l_gl_set_uniform_1f},
    {"setUniform2f", l_gl_set_uniform_2f},
    {"setUniform3f", l_gl_set_uniform_3f},
    {"setUniform4f", l_gl_set_uniform_4f},
    {"getLastError", l_gl_get_last_error},
    {"getProjectDir", l_gl_get_project_dir},
    {"createRenderTarget", l_gl_create_render_target},
    {"destroyRenderTarget", l_gl_destroy_render_target},
    {"resizeRenderTarget", l_gl_resize_render_target},
    {"drawRegion", l_gl_draw_region},
    {"bindTexture", l_gl_bind_texture},

    {"createBuffer", l_gl_create_buffer},
    {"updateBuffer", l_gl_update_buffer},
    {"destroyBuffer", l_gl_destroy_buffer},
    {"createTexture2D", l_gl_create_texture_2d},
    {"loadTexture2D", l_gl_load_texture_2d},
    {"loadTexture2DFromBuffer", l_gl_load_texture_2d_from_buffer},
    {"updateTexture2D", l_gl_update_texture_2d},
    {"destroyTexture", l_gl_destroy_texture},
    {"loadTextureCube", l_gl_load_texture_cube},
    {"createVertexLayout", l_gl_create_vertex_layout},
    {"destroyVertexLayout", l_gl_destroy_vertex_layout},
    {"setAttribute", l_gl_set_attribute},
    {"setIndexBuffer", l_gl_set_index_buffer},
    {"setUniformMatrix3", l_gl_set_uniform_matrix3},
    {"setUniformMatrix4", l_gl_set_uniform_matrix4},
    {"setUniform1iv", l_gl_set_uniform_1iv},
    {"setUniform1fv", l_gl_set_uniform_1fv},
    {"setUniform2fv", l_gl_set_uniform_2fv},
    {"setUniform3fv", l_gl_set_uniform_3fv},
    {"setUniform4fv", l_gl_set_uniform_4fv},
    {"bindTexture2D", l_gl_bind_texture_2d},
    {"bindTextureCube", l_gl_bind_texture_cube},
    {"loadTextureCubeFromBuffer", l_gl_load_texture_cube_from_buffer},
    {"drawMesh", l_gl_draw_mesh},
    {"getAttribLocation", l_gl_get_attrib_location},
    {NULL, NULL}};

void lua_gl_register(lua_State *state, int parent_index)
{
    lua_newtable(state);
    luaL_setfuncs(state, gl_funcs, 0);
    lua_setfield(state, parent_index > 0 ? parent_index : parent_index - 1, "gl");
}