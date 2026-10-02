#ifndef LUA_CANVAS_BINDINGS_H
#define LUA_CANVAS_BINDINGS_H

#include "core/graphics_activation.h"
#include <stdbool.h>
#include "graphics/skia_wrapper.h"
#include "core/input.h"

typedef struct Window Window;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        
        void *L; 

        SkiaCanvas *canvas;
        SkiaPaint *active_paint;
        SkiaFont *active_font;

        InputState *input;

        Window *window;
        char project_dir[1024];

        int width;
        int height;

        float display_density;

        int animation_callback_ref;
        bool has_animation_callback;

        SkiaPath **paths;
        int path_count;
        int path_capacity;

        SkiaFont **fonts;
        char **font_names;
        int font_count;
        int font_capacity;

        SkiaSVG **svgs;
        int svg_count;
        int svg_capacity;

        BudoGraphicsActivation activation;

        bool exit_requested;
        int exit_code;
    } LuaCanvasContext;

    LuaCanvasContext *lua_canvas_context(void *state);

    LuaCanvasContext *lua_canvas_graphics_context(void *state);

    LuaCanvasContext *lua_canvas_create(const char *project_dir);

    void lua_canvas_destroy(LuaCanvasContext *ctx);

    bool lua_canvas_load_file(LuaCanvasContext *ctx, const char *filename);

    void lua_canvas_set_context(LuaCanvasContext *ctx, SkiaCanvas *canvas,
                                InputState *input, Window *window, int width, int height,
                                float display_density);

    bool lua_canvas_call_animation(LuaCanvasContext *ctx, double timestamp);

    bool lua_canvas_has_animation(LuaCanvasContext *ctx);

    bool lua_canvas_exit_requested(const LuaCanvasContext *ctx, int *code);

#ifdef __cplusplus
}
#endif

#endif