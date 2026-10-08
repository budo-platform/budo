#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "graphics/lua_canvas_bindings.h"
#include "graphics/lua_gl_bindings.h"
#include "graphics/lua_transform_bindings.h"
#include "graphics/lua_canvas_effects_bindings.h"
#include "graphics/skia_wrapper.h"
#include "graphics/color_util.h"
#include "core/input.h"
#include "core/window.h"

static const char lua_canvas_context_registry_key;

LuaCanvasContext *lua_canvas_context(void *opaque)
{
    lua_State *L = (lua_State *)opaque;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &lua_canvas_context_registry_key);
    LuaCanvasContext *ctx = (LuaCanvasContext *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

LuaCanvasContext *lua_canvas_graphics_context(void *opaque)
{
    LuaCanvasContext *ctx = lua_canvas_context(opaque);
    
    if (ctx)
        budo_graphics_activation_request(&ctx->activation);
    return ctx;
}

#define lua_canvas_get_context(L) lua_canvas_graphics_context(L)

static void lua_canvas_store_context(lua_State *L, LuaCanvasContext *ctx)
{
    if (ctx)
        lua_pushlightuserdata(L, ctx);
    else
        lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &lua_canvas_context_registry_key);
}

static char *lua_strdup_local(const char *src)
{
    if (!src)
        return NULL;
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, src, len + 1);
    return copy;
}

static bool lua_path_is_absolute(const char *path)
{
    if (!path || !path[0])
        return false;
    return path[0] == '/' || path[0] == '\\' || (strlen(path) > 1 && path[1] == ':');
}

static bool lua_resolve_project_path(const LuaCanvasContext *ctx, const char *path,
                                     char *out, size_t out_size)
{
    int written;
    if (!path || !out || out_size == 0)
        return false;

    if (lua_path_is_absolute(path) || !ctx || !ctx->project_dir[0])
        written = snprintf(out, out_size, "%s", path);
    else
        written = snprintf(out, out_size, "%s/%s", ctx->project_dir, path);

    return written > 0 && (size_t)written < out_size;
}

static int lua_find_font_index(LuaCanvasContext *ctx, const char *name)
{
    if (!ctx || !name)
        return -1;
    for (int i = 0; i < ctx->font_count; i++)
    {
        if (ctx->font_names[i] && strcmp(ctx->font_names[i], name) == 0)
            return i;
    }
    return -1;
}

static bool lua_ensure_font_capacity(LuaCanvasContext *ctx)
{
    if (!ctx)
        return false;
    if (ctx->font_count < ctx->font_capacity)
        return true;
    int new_capacity = ctx->font_capacity > 0 ? ctx->font_capacity * 2 : 4;
    SkiaFont **fonts = (SkiaFont **)calloc((size_t)new_capacity, sizeof(SkiaFont *));
    char **font_names = (char **)calloc((size_t)new_capacity, sizeof(char *));
    if (!fonts || !font_names)
    {
        free(fonts);
        free(font_names);
        return false;
    }
    for (int i = 0; i < ctx->font_count; i++)
    {
        fonts[i] = ctx->fonts[i];
        font_names[i] = ctx->font_names[i];
    }
    free(ctx->fonts);
    free(ctx->font_names);
    ctx->fonts = fonts;
    ctx->font_names = font_names;
    ctx->font_capacity = new_capacity;
    return true;
}

static int add_path(LuaCanvasContext *ctx, SkiaPath *path)
{
    if (ctx->path_count >= ctx->path_capacity)
    {
        int new_capacity = ctx->path_capacity == 0 ? 16 : ctx->path_capacity * 2;
        SkiaPath **new_paths = (SkiaPath **)realloc(ctx->paths, new_capacity * sizeof(SkiaPath *));
        if (!new_paths)
            return -1;
        ctx->paths = new_paths;
        ctx->path_capacity = new_capacity;
    }
    int id = ctx->path_count;
    ctx->paths[ctx->path_count++] = path;
    return id;
}

static SkiaPath *get_path(LuaCanvasContext *ctx, int id)
{
    if (id < 0 || id >= ctx->path_count)
        return NULL;
    return ctx->paths[id];
}

static int add_svg(LuaCanvasContext *ctx, SkiaSVG *svg)
{
    if (ctx->svg_count >= ctx->svg_capacity)
    {
        int new_capacity = ctx->svg_capacity == 0 ? 8 : ctx->svg_capacity * 2;
        SkiaSVG **new_svgs = (SkiaSVG **)realloc(ctx->svgs, new_capacity * sizeof(SkiaSVG *));
        if (!new_svgs)
            return -1;
        ctx->svgs = new_svgs;
        ctx->svg_capacity = new_capacity;
    }
    int id = ctx->svg_count;
    ctx->svgs[ctx->svg_count++] = svg;
    return id;
}

static SkiaSVG *get_svg(LuaCanvasContext *ctx, int id)
{
    if (id < 0 || id >= ctx->svg_count)
        return NULL;
    return ctx->svgs[id];
}

static uint32_t lua_get_color(lua_State *L, int idx)
{
    if (lua_isnumber(L, idx))
    {
        return (uint32_t)(lua_Integer)lua_tointeger(L, idx);
    }
    if (lua_isstring(L, idx))
    {
        const char *str = lua_tostring(L, idx);
        uint32_t color;
        if (color_parse_hex_string(str, &color))
            return color;
    }
    return SKIA_COLOR_BLACK;
}

static int l_sys_exit(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_context(L);
    int code = (int)luaL_optinteger(L, 1, 0);
    if (ctx)
    {
        ctx->exit_requested = true;
        ctx->exit_code = code;
    }
    return 0;
}

bool lua_canvas_exit_requested(const LuaCanvasContext *ctx, int *code)
{
    if (!ctx || !ctx->exit_requested)
        return false;
    if (code)
        *code = ctx->exit_code;
    return true;
}

static int l_console_log(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++)
    {
        const char *s = luaL_tolstring(L, i, NULL);
        if (s)
        {
            fputs(s, stdout);
            if (i < n)
                fputc(' ', stdout);
        }
        lua_pop(L, 1);
    }
    fputc('\n', stdout);
    return 0;
}

static int l_canvas_clear(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas)
        return 0;
    uint32_t color = SKIA_COLOR_WHITE;
    if (lua_gettop(L) >= 1)
        color = lua_get_color(L, 1);
    skia_canvas_clear(ctx->canvas, color);
    return 0;
}

static int l_canvas_read_pixels(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas)
        return luaL_error(L, "No active canvas");

    int w = 0, h = 0;
    if (!skia_canvas_get_size(ctx->canvas, &w, &h) || w <= 0 || h <= 0)
        return luaL_error(L, "Canvas has no readable size");

    size_t bytes = (size_t)w * (size_t)h * 4u;
    uint8_t *pixels = (uint8_t *)malloc(bytes);
    if (!pixels)
        return luaL_error(L, "Out of memory");

    if (!skia_canvas_read_pixels(ctx->canvas, pixels))
    {
        free(pixels);
        return luaL_error(L, "skia_canvas_read_pixels failed");
    }

    lua_newtable(L);
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "height");
    lua_pushlstring(L, (const char *)pixels, bytes);
    lua_setfield(L, -2, "pixels");
    free(pixels);
    return 1;
}

static int l_canvas_draw_rect(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 4)
        return 0;
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    double w = luaL_checknumber(L, 3);
    double h = luaL_checknumber(L, 4);
    skia_canvas_draw_rect(ctx->canvas, x, y, x + w, y + h, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_round_rect(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 6)
        return 0;
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    double w = luaL_checknumber(L, 3);
    double h = luaL_checknumber(L, 4);
    double rx = luaL_checknumber(L, 5);
    double ry = luaL_checknumber(L, 6);
    skia_canvas_draw_round_rect(ctx->canvas, x, y, x + w, y + h, rx, ry, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_circle(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 3)
        return 0;
    double cx = luaL_checknumber(L, 1);
    double cy = luaL_checknumber(L, 2);
    double radius = luaL_checknumber(L, 3);
    skia_canvas_draw_circle(ctx->canvas, cx, cy, radius, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_oval(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 4)
        return 0;
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    double w = luaL_checknumber(L, 3);
    double h = luaL_checknumber(L, 4);
    skia_canvas_draw_oval(ctx->canvas, x, y, x + w, y + h, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_line(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 4)
        return 0;
    double x1 = luaL_checknumber(L, 1);
    double y1 = luaL_checknumber(L, 2);
    double x2 = luaL_checknumber(L, 3);
    double y2 = luaL_checknumber(L, 4);
    skia_canvas_draw_line(ctx->canvas, x1, y1, x2, y2, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_point(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 2)
        return 0;
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    skia_canvas_draw_point(ctx->canvas, x, y, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 3)
        return 0;
    const char *text = luaL_checkstring(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    double font_size = BUDO_DEFAULT_FONT_SIZE;
    if (lua_gettop(L) >= 4)
        font_size = luaL_checknumber(L, 4);
    float width = skia_canvas_draw_text_with_font(ctx->canvas, text, x, y, font_size,
                                                  ctx->active_paint, ctx->active_font);
    lua_pushnumber(L, (double)width);
    return 1;
}

static int l_canvas_measure_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    const char *text = luaL_checkstring(L, 1);
    double font_size = BUDO_DEFAULT_FONT_SIZE;
    if (lua_gettop(L) >= 2)
        font_size = luaL_checknumber(L, 2);
    float width = skia_measure_text_width(text, font_size, ctx->active_font);
    lua_pushnumber(L, (double)width);
    return 1;
}

static int l_canvas_measure_text_rect(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_newtable(L);
        lua_pushnumber(L, 0.0);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, 0.0);
        lua_setfield(L, -2, "height");
        return 1;
    }
    const char *text = luaL_checkstring(L, 1);
    double font_size = BUDO_DEFAULT_FONT_SIZE;
    if (lua_gettop(L) >= 2)
        font_size = luaL_checknumber(L, 2);
    float w = 0.0f, h = 0.0f;
    skia_measure_text_rect(text, font_size, ctx->active_font, &w, &h);
    lua_newtable(L);
    lua_pushnumber(L, (double)w);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, (double)h);
    lua_setfield(L, -2, "height");
    return 1;
}

static int l_canvas_draw_arc(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 6)
        return 0;
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    double w = luaL_checknumber(L, 3);
    double h = luaL_checknumber(L, 4);
    double start_angle = luaL_checknumber(L, 5);
    double sweep_angle = luaL_checknumber(L, 6);
    bool use_center = false;
    if (lua_gettop(L) >= 7)
        use_center = lua_toboolean(L, 7);
    skia_canvas_draw_arc(ctx->canvas, x, y, x + w, y + h,
                         start_angle, sweep_angle, use_center, ctx->active_paint);
    return 0;
}

static int l_canvas_draw_path(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_canvas_draw_path(ctx->canvas, path, ctx->active_paint);
    return 0;
}

static int l_set_fill_color(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    uint32_t color = lua_get_color(L, 1);
    skia_paint_set_color(ctx->active_paint, color);
    skia_paint_set_style(ctx->active_paint, SKIA_PAINT_FILL);
    return 0;
}

static int l_set_stroke_color(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    uint32_t color = lua_get_color(L, 1);
    skia_paint_set_color(ctx->active_paint, color);
    skia_paint_set_style(ctx->active_paint, SKIA_PAINT_STROKE);
    return 0;
}

static int l_set_stroke_width(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    double width = luaL_checknumber(L, 1);
    skia_paint_set_stroke_width(ctx->active_paint, width);
    return 0;
}

static int l_set_anti_alias(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    bool enabled = lua_toboolean(L, 1);
    skia_paint_set_anti_alias(ctx->active_paint, enabled);
    return 0;
}

static int l_set_alpha(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    double alpha = luaL_checknumber(L, 1);
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    skia_paint_set_alpha(ctx->active_paint, (uint8_t)alpha);
    return 0;
}

static int l_set_stroke_cap(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    const char *cap_str = luaL_checkstring(L, 1);
    SkiaStrokeCap cap = SKIA_STROKE_CAP_BUTT;
    if (strcmp(cap_str, "round") == 0)
        cap = SKIA_STROKE_CAP_ROUND;
    else if (strcmp(cap_str, "square") == 0)
        cap = SKIA_STROKE_CAP_SQUARE;
    skia_paint_set_stroke_cap(ctx->active_paint, cap);
    return 0;
}

static int l_set_stroke_join(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->active_paint || lua_gettop(L) < 1)
        return 0;
    const char *join_str = luaL_checkstring(L, 1);
    SkiaStrokeJoin join = SKIA_STROKE_JOIN_MITER;
    if (strcmp(join_str, "round") == 0)
        join = SKIA_STROKE_JOIN_ROUND;
    else if (strcmp(join_str, "bevel") == 0)
        join = SKIA_STROKE_JOIN_BEVEL;
    skia_paint_set_stroke_join(ctx->active_paint, join);
    return 0;
}

static const struct
{
    const char *name;
    SkiaBlendMode mode;
} k_lua_blend_mode_table[] = {
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

static int l_set_blend_mode(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    const char *mode_str = luaL_checkstring(L, 1);
    if (!ctx || !ctx->active_paint)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (size_t i = 0; i < sizeof(k_lua_blend_mode_table) / sizeof(k_lua_blend_mode_table[0]); i++)
    {
        if (strcmp(mode_str, k_lua_blend_mode_table[i].name) == 0)
        {
            skia_paint_set_blend_mode(ctx->active_paint, k_lua_blend_mode_table[i].mode);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    return luaL_error(L, "sys.canvas.setBlendMode: unknown mode '%s'", mode_str);
}

static int l_set_image_filter(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    bool clear = lua_isnoneornil(L, 1);
    const char *name = NULL;
    if (!clear)
    {
        name = luaL_checkstring(L, 1);
        if (strcmp(name, "none") == 0)
            clear = true;
    }

    if (!ctx || !ctx->active_paint)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (clear)
    {
        skia_paint_clear_image_filter(ctx->active_paint);
        lua_pushboolean(L, 1);
        return 1;
    }

    if (strcmp(name, "blur") == 0)
    {
        double sigma_x = luaL_checknumber(L, 2);
        double sigma_y = luaL_optnumber(L, 3, sigma_x);
        skia_paint_set_blur_filter(ctx->active_paint,
                                   (float)sigma_x, (float)sigma_y);
        lua_pushboolean(L, 1);
        return 1;
    }

    if (strcmp(name, "drop-shadow") == 0 || strcmp(name, "drop-shadow-only") == 0)
    {
        bool only = (strcmp(name, "drop-shadow-only") == 0);
        double dx = luaL_checknumber(L, 2);
        double dy = luaL_checknumber(L, 3);
        double sigma_x = luaL_checknumber(L, 4);
        double sigma_y;
        int color_idx;
        int top = lua_gettop(L);
        if (top < 5)
            return luaL_error(L,
                              only
                                  ? "sys.canvas.setImageFilter('drop-shadow-only', dx, dy, sigmaX, [sigmaY], color): too few arguments"
                                  : "sys.canvas.setImageFilter('drop-shadow', dx, dy, sigmaX, [sigmaY], color): too few arguments");
        if (top == 5)
        {
            sigma_y = sigma_x;
            color_idx = 5;
        }
        else
        {
            sigma_y = luaL_checknumber(L, 5);
            color_idx = 6;
        }
        uint32_t color = lua_get_color(L, color_idx);
        if (only)
            skia_paint_set_drop_shadow_only_filter(ctx->active_paint,
                                                   (float)dx, (float)dy,
                                                   (float)sigma_x, (float)sigma_y,
                                                   color);
        else
            skia_paint_set_drop_shadow_filter(ctx->active_paint,
                                              (float)dx, (float)dy,
                                              (float)sigma_x, (float)sigma_y,
                                              color);
        lua_pushboolean(L, 1);
        return 1;
    }

    return luaL_error(L, "sys.canvas.setImageFilter: unknown filter '%s'", name);
}

static int l_set_color_filter(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    bool clear = lua_isnoneornil(L, 1);
    const char *name = NULL;
    if (!clear)
    {
        name = luaL_checkstring(L, 1);
        if (strcmp(name, "none") == 0)
            clear = true;
    }

    if (!ctx || !ctx->active_paint)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (clear)
    {
        skia_paint_clear_color_filter(ctx->active_paint);
        lua_pushboolean(L, 1);
        return 1;
    }

    if (strcmp(name, "matrix") == 0)
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_Integer len = luaL_len(L, 2);
        if (len != 20)
            return luaL_error(L,
                              "sys.canvas.setColorFilter('matrix'): matrix must have exactly 20 elements (got %d)",
                              (int)len);
        float m[20];
        for (int i = 0; i < 20; i++)
        {
            lua_rawgeti(L, 2, i + 1);
            int isnum = 0;
            lua_Number v = lua_tonumberx(L, -1, &isnum);
            lua_pop(L, 1);
            if (!isnum)
                return luaL_error(L,
                                  "sys.canvas.setColorFilter('matrix'): element %d is not a number", i + 1);
            m[i] = (float)v;
        }
        skia_paint_set_color_matrix_filter(ctx->active_paint, m);
        lua_pushboolean(L, 1);
        return 1;
    }

    if (strcmp(name, "blend") == 0)
    {
        if (lua_gettop(L) < 3)
            return luaL_error(L,
                              "sys.canvas.setColorFilter('blend', color, mode): too few arguments");
        uint32_t color = lua_get_color(L, 2);
        const char *mode_str = luaL_checkstring(L, 3);
        for (size_t i = 0; i < sizeof(k_lua_blend_mode_table) / sizeof(k_lua_blend_mode_table[0]); i++)
        {
            if (strcmp(mode_str, k_lua_blend_mode_table[i].name) == 0)
            {
                skia_paint_set_blend_color_filter(ctx->active_paint,
                                                  color, k_lua_blend_mode_table[i].mode);
                lua_pushboolean(L, 1);
                return 1;
            }
        }
        return luaL_error(L,
                          "sys.canvas.setColorFilter('blend'): unknown blend mode '%s'", mode_str);
    }

    return luaL_error(L, "sys.canvas.setColorFilter: unknown filter '%s'", name);
}

static int l_path_create(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
        return 0;
    SkiaPath *path = skia_path_create();
    if (!path)
        return 0;
    int id = add_path(ctx, path);
    if (id < 0)
    {
        skia_path_destroy(path);
        return 0;
    }
    lua_pushinteger(L, id);
    return 1;
}

static int l_path_reset(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_reset(path);
    return 0;
}

static int l_path_move_to(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 3)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_move_to(path, x, y);
    return 0;
}

static int l_path_line_to(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 3)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_line_to(path, x, y);
    return 0;
}

static int l_path_quad_to(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 5)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double x1 = luaL_checknumber(L, 2);
    double y1 = luaL_checknumber(L, 3);
    double x2 = luaL_checknumber(L, 4);
    double y2 = luaL_checknumber(L, 5);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_quad_to(path, x1, y1, x2, y2);
    return 0;
}

static int l_path_cubic_to(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 7)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double x1 = luaL_checknumber(L, 2);
    double y1 = luaL_checknumber(L, 3);
    double x2 = luaL_checknumber(L, 4);
    double y2 = luaL_checknumber(L, 5);
    double x3 = luaL_checknumber(L, 6);
    double y3 = luaL_checknumber(L, 7);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_cubic_to(path, x1, y1, x2, y2, x3, y3);
    return 0;
}

static int l_path_close(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_close(path);
    return 0;
}

static int l_path_add_rect(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 5)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    double w = luaL_checknumber(L, 4);
    double h = luaL_checknumber(L, 5);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_add_rect(path, x, y, x + w, y + h);
    return 0;
}

static int l_path_add_circle(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 4)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double cx = luaL_checknumber(L, 2);
    double cy = luaL_checknumber(L, 3);
    double radius = luaL_checknumber(L, 4);
    SkiaPath *path = get_path(ctx, id);
    if (path)
        skia_path_add_circle(path, cx, cy, radius);
    return 0;
}

static int l_svg_load(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    const char *path = luaL_checkstring(L, 1);
    char resolved[1024];
    if (!lua_resolve_project_path(ctx, path, resolved, sizeof(resolved)))
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    SkiaSVG *svg = skia_svg_load_file(resolved);
    if (!svg)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    int id = add_svg(ctx, svg);
    if (id < 0)
    {
        skia_svg_destroy(svg);
        lua_pushinteger(L, -1);
        return 1;
    }
    lua_pushinteger(L, id);
    return 1;
}

static int l_svg_load_from_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    size_t len = 0;
    const char *data = luaL_checklstring(L, 1, &len);
    SkiaSVG *svg = skia_svg_load_buffer(data, len);
    if (!svg)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    int id = add_svg(ctx, svg);
    if (id < 0)
    {
        skia_svg_destroy(svg);
        lua_pushinteger(L, -1);
        return 1;
    }
    lua_pushinteger(L, id);
    return 1;
}

static int l_svg_destroy(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    SkiaSVG *svg = get_svg(ctx, id);
    if (svg)
    {
        skia_svg_destroy(svg);
        ctx->svgs[id] = NULL;
    }
    return 0;
}

static int l_svg_draw(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->canvas || lua_gettop(L) < 5)
        return 0;
    int id = (int)luaL_checkinteger(L, 1);
    double x = luaL_checknumber(L, 2);
    double y = luaL_checknumber(L, 3);
    double w = luaL_checknumber(L, 4);
    double h = luaL_checknumber(L, 5);
    SkiaSVG *svg = get_svg(ctx, id);
    if (svg)
        skia_svg_render(svg, ctx->canvas, x, y, w, h);
    return 0;
}

static int l_svg_get_width(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushnumber(L, 0);
        return 1;
    }
    int id = (int)luaL_checkinteger(L, 1);
    SkiaSVG *svg = get_svg(ctx, id);
    if (svg)
        lua_pushnumber(L, skia_svg_get_width(svg));
    else
        lua_pushnumber(L, 0);
    return 1;
}

static int l_svg_get_height(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushnumber(L, 0);
        return 1;
    }
    int id = (int)luaL_checkinteger(L, 1);
    SkiaSVG *svg = get_svg(ctx, id);
    if (svg)
        lua_pushnumber(L, skia_svg_get_height(svg));
    else
        lua_pushnumber(L, 0);
    return 1;
}

static int l_font_load(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char *path = luaL_checkstring(L, 1);
    const char *name = NULL;
    if (lua_gettop(L) >= 2)
        name = luaL_checkstring(L, 2);

    char resolved_path[2048];
    if (!lua_resolve_project_path(ctx, path, resolved_path, sizeof(resolved_path)))
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    SkiaFont *font = skia_font_load_file(resolved_path);
    if (!font)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char *font_name = name ? name : path;
    int index = lua_find_font_index(ctx, font_name);
    if (index >= 0)
    {
        if (ctx->active_font == ctx->fonts[index])
            ctx->active_font = font;
        skia_font_destroy(ctx->fonts[index]);
        ctx->fonts[index] = font;
    }
    else
    {
        if (!lua_ensure_font_capacity(ctx))
        {
            skia_font_destroy(font);
            lua_pushboolean(L, 0);
            return 1;
        }
        char *stored_name = lua_strdup_local(font_name);
        if (!stored_name)
        {
            skia_font_destroy(font);
            lua_pushboolean(L, 0);
            return 1;
        }
        ctx->fonts[ctx->font_count] = font;
        ctx->font_names[ctx->font_count] = stored_name;
        ctx->font_count += 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_font_load_from_buffer(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || lua_gettop(L) < 1)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    size_t len = 0;
    const char *data = luaL_checklstring(L, 1, &len);
    const char *name = NULL;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
        name = luaL_checkstring(L, 2);

    SkiaFont *font = skia_font_load_buffer(data, len);
    if (!font)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    const char *font_name = name ? name : "buffer";
    int index = lua_find_font_index(ctx, font_name);
    if (index >= 0)
    {
        if (ctx->active_font == ctx->fonts[index])
            ctx->active_font = font;
        skia_font_destroy(ctx->fonts[index]);
        ctx->fonts[index] = font;
    }
    else
    {
        if (!lua_ensure_font_capacity(ctx))
        {
            skia_font_destroy(font);
            lua_pushboolean(L, 0);
            return 1;
        }
        char *stored_name = lua_strdup_local(font_name);
        if (!stored_name)
        {
            skia_font_destroy(font);
            lua_pushboolean(L, 0);
            return 1;
        }
        ctx->fonts[ctx->font_count] = font;
        ctx->font_names[ctx->font_count] = stored_name;
        ctx->font_count += 1;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_font_set(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    if (lua_isnoneornil(L, 1))
    {
        ctx->active_font = NULL;
        lua_pushboolean(L, 1);
        return 1;
    }
    const char *name = luaL_checkstring(L, 1);
    int index = lua_find_font_index(ctx, name);
    if (index < 0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    ctx->active_font = ctx->fonts[index];
    lua_pushboolean(L, 1);
    return 1;
}

static void lua_push_pointer(lua_State *L, int id, int x, int y, int dx, int dy,
                             bool down, bool pressed, const char *type)
{
    lua_newtable(L);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, dx);
    lua_setfield(L, -2, "dx");
    lua_pushinteger(L, dy);
    lua_setfield(L, -2, "dy");
    lua_pushboolean(L, down);
    lua_setfield(L, -2, "down");
    lua_pushboolean(L, pressed);
    lua_setfield(L, -2, "pressed");
    lua_pushstring(L, type);
    lua_setfield(L, -2, "type");
}

static int l_input_get(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->input)
    {
        lua_pushnil(L);
        return 1;
    }

    InputState *input = ctx->input;

    lua_newtable(L); 

    lua_newtable(L);
    lua_pushinteger(L, input->mouse_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, input->mouse_y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, input->mouse_dx);
    lua_setfield(L, -2, "dx");
    lua_pushinteger(L, input->mouse_dy);
    lua_setfield(L, -2, "dy");
    lua_pushinteger(L, input->mouse_wheel_x);
    lua_setfield(L, -2, "wheelX");
    lua_pushinteger(L, input->mouse_wheel_y);
    lua_setfield(L, -2, "wheelY");
    lua_pushboolean(L, input->mouse_buttons[INPUT_MOUSE_LEFT]);
    lua_setfield(L, -2, "left");
    lua_pushboolean(L, input->mouse_buttons[INPUT_MOUSE_MIDDLE]);
    lua_setfield(L, -2, "middle");
    lua_pushboolean(L, input->mouse_buttons[INPUT_MOUSE_RIGHT]);
    lua_setfield(L, -2, "right");
    lua_pushboolean(L, input->mouse_buttons_pressed[INPUT_MOUSE_LEFT]);
    lua_setfield(L, -2, "leftPressed");
    lua_pushboolean(L, input->mouse_buttons_pressed[INPUT_MOUSE_RIGHT]);
    lua_setfield(L, -2, "rightPressed");
    lua_setfield(L, -2, "mouse");

    int touch_count = input_touch_count(input);
    lua_push_pointer(L, 0, input->mouse_x, input->mouse_y,
                     input->mouse_dx, input->mouse_dy,
                     input->mouse_buttons[INPUT_MOUSE_LEFT],
                     input->mouse_buttons_pressed[INPUT_MOUSE_LEFT],
                     touch_count > 0 ? "touch" : "mouse");
    lua_setfield(L, -2, "pointer");

    lua_newtable(L);
    if (touch_count > 0)
    {
        for (int i = 0; i < touch_count; i++)
        {
            const InputTouchPoint *touch = input_touch_get(input, i);
            if (!touch)
                continue;
            lua_push_pointer(L, touch->id, touch->x, touch->y,
                             touch->dx, touch->dy, true, touch->pressed, "touch");
            lua_rawseti(L, -2, i + 1); 
        }
    }
    else if (input->mouse_buttons[INPUT_MOUSE_LEFT])
    {
        lua_push_pointer(L, 0, input->mouse_x, input->mouse_y,
                         input->mouse_dx, input->mouse_dy, true,
                         input->mouse_buttons_pressed[INPUT_MOUSE_LEFT], "mouse");
        lua_rawseti(L, -2, 1);
    }
    lua_setfield(L, -2, "pointers");

    lua_newtable(L);
    lua_pushboolean(L, input->shift);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, input->ctrl);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, input->alt);
    lua_setfield(L, -2, "alt");
    lua_pushboolean(L, input->meta);
    lua_setfield(L, -2, "meta");
    lua_setfield(L, -2, "keyboard");

    lua_pushlstring(L, input->text, input->text_length);
    lua_setfield(L, -2, "text");

    if (input->text_edit_changed)
    {
        lua_newtable(L);
        lua_pushlstring(L, input->text_value, input->text_value_length);
        lua_setfield(L, -2, "text");
        lua_pushinteger(L, input->text_selection_start);
        lua_setfield(L, -2, "selectionStart");
        lua_pushinteger(L, input->text_selection_end);
        lua_setfield(L, -2, "selectionEnd");
    }
    else
    {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "textEdit");

    lua_newtable(L);
    lua_pushboolean(L, input->composition_active);
    lua_setfield(L, -2, "active");
    lua_pushboolean(L, input->composition_changed);
    lua_setfield(L, -2, "changed");
    lua_pushlstring(L, input->composition_text,
                    input->composition_text_length);
    lua_setfield(L, -2, "text");
    lua_pushinteger(L, input->composition_selection_start);
    lua_setfield(L, -2, "selectionStart");
    lua_pushinteger(L, input->composition_selection_end);
    lua_setfield(L, -2, "selectionEnd");
    lua_setfield(L, -2, "composition");
    lua_pushboolean(L, input->text_session_active);
    lua_setfield(L, -2, "textInputActive");
    lua_pushboolean(L, input->text_platform.native_editing);
    lua_setfield(L, -2, "nativeTextEditing");

    lua_pushnumber(L, input->delta_time);
    lua_setfield(L, -2, "deltaTime");
    lua_pushnumber(L, input->total_time);
    lua_setfield(L, -2, "totalTime");
    lua_pushinteger(L, (lua_Integer)input->frame_count);
    lua_setfield(L, -2, "frameCount");

    lua_pushboolean(L, input->window_focused);
    lua_setfield(L, -2, "focused");

    return 1;
}

static int l_input_is_key_down(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->input || lua_gettop(L) < 1)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int scancode = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input_key_down(ctx->input, scancode));
    return 1;
}

static int l_input_is_key_pressed(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx || !ctx->input || lua_gettop(L) < 1)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int scancode = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, input_key_pressed(ctx->input, scancode));
    return 1;
}

static void l_text_options(lua_State *L, int *start, int *end,
                           int *x, int *y, int *width, int *height)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "selectionStart");
    *start = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "selectionEnd");
    *end = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    if (!x)
        return;
    lua_getfield(L, 1, "caret");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "x");
        *x = (int)luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        lua_getfield(L, -1, "y");
        *y = (int)luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        lua_getfield(L, -1, "width");
        *width = (int)luaL_optinteger(L, -1, 1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "height");
        *height = (int)luaL_optinteger(L, -1, 1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

static int l_input_start_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    int start;
    int end;
    bool multiline;
    const char *text;
    if (!ctx || !ctx->input)
        return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "text");
    text = luaL_checkstring(L, -1);
    l_text_options(L, &start, &end, NULL, NULL, NULL, NULL);
    lua_getfield(L, 1, "multiline");
    multiline = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_pushboolean(L, input_text_start(ctx->input, text,
                                        start, end, multiline));
    return 1;
}

static int l_input_update_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    int start;
    int end;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    const char *text;
    if (!ctx || !ctx->input)
        return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "text");
    text = luaL_checkstring(L, -1);
    l_text_options(L, &start, &end, &x, &y, &width, &height);
    lua_pushboolean(L, input_text_update(ctx->input, text,
                                         start, end, x, y, width, height));
    return 1;
}

static int l_input_stop_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (ctx && ctx->input)
        input_text_stop(ctx->input);
    return 0;
}

static int l_get_width(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, ctx->width);
    return 1;
}

static int l_get_height(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, ctx->height);
    return 1;
}

static int l_get_display_density(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
    {
        lua_pushnumber(L, 1.0);
        return 1;
    }
    lua_pushnumber(L, (double)ctx->display_density);
    return 1;
}

static int l_animation_start(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
        return 0;

    if (ctx->has_animation_callback)
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->animation_callback_ref);

    lua_pushvalue(L, 1);
    ctx->animation_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ctx->has_animation_callback = true;

    return 0;
}

static int animation_set_callback(lua_State *L, bool wait)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
        return 0;

    if (ctx->has_animation_callback)
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->animation_callback_ref);

    lua_pushvalue(L, 1);
    ctx->animation_callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ctx->has_animation_callback = true;
    if (wait)
        budo_animation_wait_start(&ctx->animation_wait, luaL_optnumber(L, 2, 0.0), ctx->width, ctx->height);
    else
        budo_animation_wait_stop(&ctx->animation_wait);

    lua_pushinteger(L, 1);
    return 1;
}

static int l_animation_request_frame(lua_State *L)
{
    return animation_set_callback(L, false);
}

static int l_animation_wait_for_input(lua_State *L)
{
    return animation_set_callback(L, true);
}

BudoAnimationWait *lua_canvas_animation_wait(LuaCanvasContext *ctx) { return ctx ? &ctx->animation_wait : NULL; }

static int l_animation_cancel_frame(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    if (!ctx)
        return 0;

    if (ctx->has_animation_callback)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, ctx->animation_callback_ref);
        ctx->animation_callback_ref = LUA_NOREF;
        ctx->has_animation_callback = false;
    }
    return 0;
}

 static int l_transform_save(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    if (context && context->canvas)
        skia_canvas_save(context->canvas);
    return 0;
}

static int l_transform_restore(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    if (context && context->canvas)
        skia_canvas_restore(context->canvas);
    return 0;
}

static int l_transform_translate(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    if (context && context->canvas && lua_gettop(state) >= 2)
        skia_canvas_translate(context->canvas,
                              luaL_checknumber(state, 1),
                              luaL_checknumber(state, 2));
    return 0;
}

static int l_transform_rotate(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    double degrees;
    if (!context || !context->canvas || lua_gettop(state) < 1)
        return 0;
    degrees = luaL_checknumber(state, 1);
    if (lua_gettop(state) >= 3)
        skia_canvas_rotate_around(context->canvas, degrees,
                                  luaL_checknumber(state, 2),
                                  luaL_checknumber(state, 3));
    else
        skia_canvas_rotate(context->canvas, degrees);
    return 0;
}

static int l_transform_scale(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    if (context && context->canvas && lua_gettop(state) >= 2)
        skia_canvas_scale(context->canvas,
                          luaL_checknumber(state, 1),
                          luaL_checknumber(state, 2));
    return 0;
}

static int l_transform_skew(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    if (context && context->canvas && lua_gettop(state) >= 2)
        skia_canvas_skew(context->canvas,
                         luaL_checknumber(state, 1),
                         luaL_checknumber(state, 2));
    return 0;
}

static int l_transform_reset(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    if (context && context->canvas)
        skia_canvas_reset_transform(context->canvas);
    return 0;
}

static int l_transform_clip_rect(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    double x, y, width, height;
    if (!context || !context->canvas || lua_gettop(state) < 4)
        return 0;
    x = luaL_checknumber(state, 1);
    y = luaL_checknumber(state, 2);
    width = luaL_checknumber(state, 3);
    height = luaL_checknumber(state, 4);
    skia_canvas_clip_rect(context->canvas, x, y, x + width, y + height);
    return 0;
}

static const luaL_Reg canvas_funcs[] = {
    {"clear", l_canvas_clear},
    {"readPixels", l_canvas_read_pixels},
    {"drawRect", l_canvas_draw_rect},
    {"drawRoundRect", l_canvas_draw_round_rect},
    {"drawCircle", l_canvas_draw_circle},
    {"drawOval", l_canvas_draw_oval},
    {"drawLine", l_canvas_draw_line},
    {"drawPoint", l_canvas_draw_point},
    {"drawText", l_canvas_draw_text},
    {"measureText", l_canvas_measure_text},
    {"measureTextRect", l_canvas_measure_text_rect},
    {"drawArc", l_canvas_draw_arc},
    {"setFillColor", l_set_fill_color},
    {"setStrokeColor", l_set_stroke_color},
    {"setStrokeWidth", l_set_stroke_width},
    {"setAntiAlias", l_set_anti_alias},
    {"setAlpha", l_set_alpha},
    {"setStrokeCap", l_set_stroke_cap},
    {"setStrokeJoin", l_set_stroke_join},
    {"setBlendMode", l_set_blend_mode},
    {"setImageFilter", l_set_image_filter},
    {"setColorFilter", l_set_color_filter},
    {"drawPath", l_canvas_draw_path},
    {"drawSvg", l_svg_draw},
    {"setFont", l_font_set},
    {"save", l_transform_save},
    {"restore", l_transform_restore},
    {"translate", l_transform_translate},
    {"rotate", l_transform_rotate},
    {"scale", l_transform_scale},
    {"skew", l_transform_skew},
    {"reset", l_transform_reset},
    {"clipRect", l_transform_clip_rect},
    {NULL, NULL}};

static const luaL_Reg path_funcs[] = {
    {"create", l_path_create},
    {"reset", l_path_reset},
    {"moveTo", l_path_move_to},
    {"lineTo", l_path_line_to},
    {"quadTo", l_path_quad_to},
    {"cubicTo", l_path_cubic_to},
    {"close", l_path_close},
    {"addRect", l_path_add_rect},
    {"addCircle", l_path_add_circle},
    {NULL, NULL}};

static const luaL_Reg svg_funcs[] = {
    {"load", l_svg_load},
    {"loadFromBuffer", l_svg_load_from_buffer},
    {"destroy", l_svg_destroy},
    {"getWidth", l_svg_get_width},
    {"getHeight", l_svg_get_height},
    {NULL, NULL}};

static const luaL_Reg font_funcs[] = {
    {"load", l_font_load},
    {"loadFromBuffer", l_font_load_from_buffer},
    {NULL, NULL}};

static const luaL_Reg input_funcs[] = {
    {"get", l_input_get},
    {"isKeyDown", l_input_is_key_down},
    {"isKeyPressed", l_input_is_key_pressed},
    {"startTextInput", l_input_start_text},
    {"updateTextInput", l_input_update_text},
    {"stopTextInput", l_input_stop_text},
    {NULL, NULL}};

static const luaL_Reg window_funcs[] = {
    {"getWidth", l_get_width},
    {"getHeight", l_get_height},
    {"getDisplayDensity", l_get_display_density},
    {NULL, NULL}};

static const luaL_Reg animation_funcs[] = {
    {"start", l_animation_start},
    {"requestFrame", l_animation_request_frame},
    {"waitForInput", l_animation_wait_for_input},
    {"cancelFrame", l_animation_cancel_frame},
    {NULL, NULL}};

static void register_subtable(lua_State *L, int parent_idx, const char *name,
                              const luaL_Reg *funcs)
{
    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_setfield(L, parent_idx > 0 ? parent_idx : parent_idx - 1, name);
}

LuaCanvasContext *lua_canvas_create(const char *project_dir)
{
    LuaCanvasContext *ctx = (LuaCanvasContext *)calloc(1, sizeof(LuaCanvasContext));
    if (!ctx)
        return NULL;

    lua_State *L = luaL_newstate();
    if (!L)
    {
        free(ctx);
        return NULL;
    }

    luaL_openlibs(L);

    ctx->L = L;
    ctx->animation_callback_ref = LUA_NOREF;
    ctx->has_animation_callback = false;

    ctx->active_paint = skia_paint_create();
    if (!ctx->active_paint)
    {
        lua_close(L);
        free(ctx);
        return NULL;
    }

    ctx->active_font = NULL;

    if (project_dir)
    {
        strncpy(ctx->project_dir, project_dir, sizeof(ctx->project_dir) - 1);
        ctx->project_dir[sizeof(ctx->project_dir) - 1] = '\0';
    }

    lua_canvas_store_context(L, ctx);

    lua_newtable(L);
    lua_pushcfunction(L, l_console_log);
    lua_setfield(L, -2, "log");
    lua_setglobal(L, "console");

    lua_newtable(L); 

    register_subtable(L, -1, "canvas", canvas_funcs);
    lua_transform_register(L, -1);
    register_subtable(L, -1, "path", path_funcs);
    lua_canvas_effects_register(L, -1);
    register_subtable(L, -1, "svg", svg_funcs);
    register_subtable(L, -1, "font", font_funcs);
    lua_gl_register(L, -1);
    register_subtable(L, -1, "input", input_funcs);
    register_subtable(L, -1, "window", window_funcs);
    register_subtable(L, -1, "animation", animation_funcs);
    lua_pushcfunction(L, l_sys_exit);
    lua_setfield(L, -2, "exit");

    lua_setglobal(L, "sys");

    return ctx;
}

void lua_canvas_destroy(LuaCanvasContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->input)
        input_text_stop(ctx->input);

    lua_State *L = (lua_State *)ctx->L;
    if (L)
    {
        if (ctx->has_animation_callback)
            luaL_unref(L, LUA_REGISTRYINDEX, ctx->animation_callback_ref);
        lua_canvas_store_context(L, NULL);
        lua_close(L);
    }

    for (int i = 0; i < ctx->path_count; i++)
        skia_path_destroy(ctx->paths[i]);
    free(ctx->paths);

    for (int i = 0; i < ctx->svg_count; i++)
    {
        if (ctx->svgs[i])
            skia_svg_destroy(ctx->svgs[i]);
    }
    free(ctx->svgs);

    for (int i = 0; i < ctx->font_count; i++)
    {
        skia_font_destroy(ctx->fonts[i]);
        free(ctx->font_names[i]);
    }
    free(ctx->fonts);
    free(ctx->font_names);

    if (ctx->active_paint)
        skia_paint_destroy(ctx->active_paint);

    free(ctx);
}

bool lua_canvas_load_file(LuaCanvasContext *ctx, const char *filename)
{
    if (!ctx || !filename)
        return false;

    lua_State *L = (lua_State *)ctx->L;

    if (luaL_loadfile(L, filename) != LUA_OK)
    {
        fprintf(stderr, "Lua load error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        fprintf(stderr, "Lua runtime error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    return true;
}

void lua_canvas_set_context(LuaCanvasContext *ctx, SkiaCanvas *canvas,
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

bool lua_canvas_call_animation(LuaCanvasContext *ctx, double timestamp)
{
    if (!ctx || !ctx->has_animation_callback)
        return false;

    lua_State *L = (lua_State *)ctx->L;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->animation_callback_ref);
    lua_pushnumber(L, timestamp);

    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
    {
        fprintf(stderr, "Lua animation error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    return true;
}

bool lua_canvas_has_animation(LuaCanvasContext *ctx)
{
    return ctx && ctx->has_animation_callback;
}