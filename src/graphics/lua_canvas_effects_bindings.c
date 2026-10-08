#include "graphics/lua_canvas_effects_bindings.h"
#include "graphics/lua_canvas_bindings.h"
#include "graphics/color_util.h"

#include "lauxlib.h"
#include "lua.h"

#include <stdbool.h>
#include <string.h>

#define lua_canvas_get_context(L) lua_canvas_graphics_context(L)

static uint32_t lua_get_color(lua_State *L, int index)
{
    uint32_t color = SKIA_COLOR_BLACK;
    if (lua_isnumber(L, index))
        return (uint32_t)(lua_Integer)lua_tointeger(L, index);
    if (lua_isstring(L, index))
        color_parse_hex_string(lua_tostring(L, index), &color);
    return color;
}

static int lua_read_gradient(lua_State *L, int colors_index, uint32_t *colors, float *stops, bool *has_stops)
{
    luaL_checktype(L, colors_index, LUA_TTABLE);
    int count = (int)lua_rawlen(L, colors_index);
    if (count < 2 || count > SKIA_GRADIENT_MAX_STOPS)
        luaL_error(L, "sys.canvas.setGradient: colors must hold 2 to %d colors", SKIA_GRADIENT_MAX_STOPS);
    for (int i = 0; i < count; i++)
    {
        lua_rawgeti(L, colors_index, i + 1);
        colors[i] = lua_get_color(L, -1);
        lua_pop(L, 1);
    }
    *has_stops = !lua_isnoneornil(L, colors_index + 1);
    if (*has_stops)
    {
        luaL_checktype(L, colors_index + 1, LUA_TTABLE);
        if ((int)lua_rawlen(L, colors_index + 1) != count)
            luaL_error(L, "sys.canvas.setGradient: stops must hold one number per color");
        for (int i = 0; i < count; i++)
        {
            lua_rawgeti(L, colors_index + 1, i + 1);
            if (!lua_isnumber(L, -1))
                luaL_error(L, "sys.canvas.setGradient: stops must be numbers");
            stops[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    return count;
}

static int l_set_gradient(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    const char *kind = lua_isnoneornil(L, 1) ? "none" : luaL_checkstring(L, 1);
    int numbers = strcmp(kind, "linear") == 0 ? 4 : strcmp(kind, "radial") == 0 ? 3
                                               : strcmp(kind, "sweep") == 0    ? 2
                                               : strcmp(kind, "none") == 0     ? 0
                                                                               : -1;
    if (numbers < 0)
        return luaL_error(L, "sys.canvas.setGradient: unknown gradient '%s'", kind);
    if (!ctx || !ctx->active_paint)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (numbers == 0)
    {
        skia_paint_clear_shader(ctx->active_paint);
        lua_pushboolean(L, 1);
        return 1;
    }
    float values[4];
    for (int i = 0; i < numbers; i++)
        values[i] = (float)luaL_checknumber(L, 2 + i);
    uint32_t colors[SKIA_GRADIENT_MAX_STOPS];
    float stops[SKIA_GRADIENT_MAX_STOPS];
    bool has_stops = false;
    int count = lua_read_gradient(L, 2 + numbers, colors, stops, &has_stops);
    const float *stop_values = has_stops ? stops : NULL;
    bool ok;
    if (numbers == 4)
        ok = skia_paint_set_linear_gradient(ctx->active_paint, values[0], values[1], values[2], values[3],
                                            colors, stop_values, count);
    else if (numbers == 3)
        ok = skia_paint_set_radial_gradient(ctx->active_paint, values[0], values[1], values[2],
                                            colors, stop_values, count);
    else
        ok = skia_paint_set_sweep_gradient(ctx->active_paint, values[0], values[1], colors, stop_values, count);
    lua_pushboolean(L, ok);
    return 1;
}

static void lua_paragraph_options(lua_State *L, int index, SkiaTextAlign *align, float *line_height,
                                  int *max_lines)
{
    *align = SKIA_TEXT_ALIGN_LEFT;
    *line_height = SKIA_DEFAULT_LINE_HEIGHT;
    *max_lines = 0;
    if (lua_isnoneornil(L, index))
        return;
    luaL_checktype(L, index, LUA_TTABLE);
    lua_getfield(L, index, "align");
    if (!lua_isnil(L, -1))
    {
        const char *name = luaL_checkstring(L, -1);
        if (strcmp(name, "center") == 0)
            *align = SKIA_TEXT_ALIGN_CENTER;
        else if (strcmp(name, "right") == 0)
            *align = SKIA_TEXT_ALIGN_RIGHT;
        else if (strcmp(name, "left") != 0)
            luaL_error(L, "sys.canvas paragraph align must be 'left', 'center' or 'right'");
    }
    lua_pop(L, 1);
    lua_getfield(L, index, "lineHeight");
    if (!lua_isnil(L, -1))
        *line_height = (float)luaL_checknumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "maxLines");
    if (!lua_isnil(L, -1))
        *max_lines = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
}

static int lua_push_paragraph(lua_State *L, SkiaParagraphMetrics metrics)
{
    lua_newtable(L);
    lua_pushnumber(L, metrics.width);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, metrics.height);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, metrics.lines);
    lua_setfield(L, -2, "lines");
    return 1;
}

static int l_canvas_draw_paragraph(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    const char *text = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float width = (float)luaL_optnumber(L, 4, 0.0);
    float font_size = (float)luaL_optnumber(L, 5, BUDO_DEFAULT_FONT_SIZE);
    SkiaTextAlign align;
    float line_height;
    int max_lines;
    lua_paragraph_options(L, 6, &align, &line_height, &max_lines);
    SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
    if (ctx && ctx->canvas)
        metrics = skia_canvas_draw_paragraph(ctx->canvas, text, x, y, width, font_size, line_height, align,
                                             max_lines, ctx->active_paint, ctx->active_font);
    return lua_push_paragraph(L, metrics);
}

static int l_canvas_measure_paragraph(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    const char *text = luaL_checkstring(L, 1);
    float width = (float)luaL_optnumber(L, 2, 0.0);
    float font_size = (float)luaL_optnumber(L, 3, BUDO_DEFAULT_FONT_SIZE);
    SkiaTextAlign align;
    float line_height;
    int max_lines;
    lua_paragraph_options(L, 4, &align, &line_height, &max_lines);
    return lua_push_paragraph(L, skia_measure_paragraph(text, width, font_size, line_height, max_lines,
                                                        ctx ? ctx->active_font : NULL));
}

static int l_transform_clip_round_rect(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    double x = luaL_checknumber(state, 1);
    double y = luaL_checknumber(state, 2);
    double width = luaL_checknumber(state, 3);
    double height = luaL_checknumber(state, 4);
    double rx = luaL_checknumber(state, 5);
    double ry = luaL_optnumber(state, 6, rx);
    if (context && context->canvas)
        skia_canvas_clip_round_rect(context->canvas, x, y, x + width, y + height, rx, ry);
    return 0;
}

static int l_transform_clip_path(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    int id = (int)luaL_checkinteger(state, 1);
    if (!context || !context->canvas)
        return 0;
    SkiaPath *path = id >= 0 && id < context->path_count ? context->paths[id] : NULL;
    if (!path)
        return luaL_error(state, "sys.canvas.clipPath: unknown path %d", id);
    skia_canvas_clip_path(context->canvas, path);
    return 0;
}

static int l_transform_save_layer(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    double alpha = luaL_optnumber(state, 1, 255.0);
    bool has_bounds = !lua_isnoneornil(state, 2);
    SkiaRect rect = {0.0f, 0.0f, 0.0f, 0.0f};
    if (has_bounds)
    {
        double x = luaL_checknumber(state, 2);
        double y = luaL_checknumber(state, 3);
        rect.left = (float)x;
        rect.top = (float)y;
        rect.right = (float)(x + luaL_checknumber(state, 4));
        rect.bottom = (float)(y + luaL_checknumber(state, 5));
    }
    double backdrop = luaL_optnumber(state, 6, 0.0);
    alpha = alpha < 0.0 ? 0.0 : alpha > 255.0 ? 255.0 : alpha;
    if (context && context->canvas)
        skia_canvas_save_layer(context->canvas, has_bounds ? &rect : NULL, (uint8_t)(alpha + 0.5),
                               (float)backdrop);
    return 0;
}

#define RICH_TEXT_MAX_SPANS 64

static int lua_rich_text_read(lua_State *L, LuaCanvasContext *ctx, int index, float default_size,
                              SkiaTextSpan *spans)
{
    luaL_checktype(L, index, LUA_TTABLE);
    int count = (int)lua_rawlen(L, index);
    if (count > RICH_TEXT_MAX_SPANS)
        return luaL_error(L, "sys.canvas rich text: at most %d spans", RICH_TEXT_MAX_SPANS);
    for (int i = 0; i < count; i++)
    {
        SkiaTextSpan span = {NULL, default_size, ctx ? ctx->active_font : NULL, 0, false};
        lua_rawgeti(L, index, i + 1);
        if (lua_type(L, -1) == LUA_TSTRING)
            span.text = lua_tostring(L, -1);
        else if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "text");
            span.text = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "size");
            if (lua_isnumber(L, -1))
                span.size = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "color");
            if (!lua_isnil(L, -1))
            {
                span.color = lua_get_color(L, -1);
                span.has_color = true;
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "font");
            if (!lua_isnil(L, -1))
            {
                const char *name = lua_tostring(L, -1);
                span.font = NULL;
                for (int f = 0; ctx && name && f < ctx->font_count; f++)
                    if (ctx->font_names[f] && strcmp(ctx->font_names[f], name) == 0)
                        span.font = ctx->fonts[f];
                if (!span.font)
                    return luaL_error(L, "sys.canvas rich text: unknown font '%s'", name ? name : "");
            }
            lua_pop(L, 1);
        }
        if (!span.text)
            return luaL_error(L, "sys.canvas rich text: span %d has no text", i + 1);
        
        lua_pop(L, 1);
        spans[i] = span;
    }
    return count;
}

static float lua_rich_text_size(lua_State *L, int index)
{
    float size = (float)BUDO_DEFAULT_FONT_SIZE;
    if (lua_istable(L, index))
    {
        lua_getfield(L, index, "size");
        if (lua_isnumber(L, -1))
            size = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    return size;
}

static int l_canvas_draw_rich_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    SkiaTextSpan spans[RICH_TEXT_MAX_SPANS];
    SkiaTextAlign align;
    float line_height;
    int max_lines;
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float width = (float)luaL_optnumber(L, 4, 0.0);
    lua_paragraph_options(L, 5, &align, &line_height, &max_lines);
    int count = lua_rich_text_read(L, ctx, 1, lua_rich_text_size(L, 5), spans);
    SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
    if (ctx && ctx->canvas)
        metrics = skia_canvas_draw_rich_text(ctx->canvas, spans, count, x, y, width, line_height, align, max_lines,
                                             ctx->active_paint);
    return lua_push_paragraph(L, metrics);
}

static int l_canvas_measure_rich_text(lua_State *L)
{
    LuaCanvasContext *ctx = lua_canvas_get_context(L);
    SkiaTextSpan spans[RICH_TEXT_MAX_SPANS];
    SkiaTextAlign align;
    float line_height;
    int max_lines;
    float width = (float)luaL_optnumber(L, 2, 0.0);
    lua_paragraph_options(L, 3, &align, &line_height, &max_lines);
    int count = lua_rich_text_read(L, ctx, 1, lua_rich_text_size(L, 3), spans);
    return lua_push_paragraph(L, skia_measure_rich_text(spans, count, width, line_height, max_lines));
}

static int l_path_add_svg(lua_State *state)
{
    LuaCanvasContext *context = lua_canvas_graphics_context(state);
    int id = (int)luaL_checkinteger(state, 1);
    const char *data = luaL_checkstring(state, 2);
    SkiaPath *path = context && id >= 0 && id < context->path_count ? context->paths[id] : NULL;
    lua_pushboolean(state, path && skia_path_add_svg(path, data));
    return 1;
}

static const luaL_Reg path_effect_funcs[] = {
    {"addSvg", l_path_add_svg},
    {NULL, NULL}};

static const luaL_Reg canvas_effect_funcs[] = {
    {"clipRoundRect", l_transform_clip_round_rect},
    {"clipPath", l_transform_clip_path},
    {"saveLayer", l_transform_save_layer},
    {"setGradient", l_set_gradient},
    {"drawParagraph", l_canvas_draw_paragraph},
    {"measureParagraph", l_canvas_measure_paragraph},
    {"drawRichText", l_canvas_draw_rich_text},
    {"measureRichText", l_canvas_measure_rich_text},
    {NULL, NULL}};

void lua_canvas_effects_register(lua_State *state, int sys_index)
{
    lua_getfield(state, sys_index, "canvas");
    luaL_setfuncs(state, canvas_effect_funcs, 0);
    lua_pop(state, 1);
    lua_getfield(state, sys_index, "path");
    luaL_setfuncs(state, path_effect_funcs, 0);
    lua_pop(state, 1);
}