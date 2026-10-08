#ifndef JS_CANVAS_BINDINGS_H
#define JS_CANVAS_BINDINGS_H

#include "core/animation_wait.h"
#include "graphics/js_core_bindings.h"
#include "quickjs.h"
#include "graphics/skia_wrapper.h"
#include "core/input.h"
#include "core/graphics_activation.h"

typedef struct Window Window;
typedef struct CanvasTexture CanvasTexture;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        JSContext *context;

        InputState *input;

        Window *window;

        int width;
        int height;

        float display_density;

        JSValue animation_callback;
        bool has_animation_callback;
        BudoAnimationWait animation_wait;

        SkiaDrawingDesk drawingDesk;

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

        CanvasTexture **canvas_textures;
        int canvas_texture_count;
        int canvas_texture_capacity;

        BudoGraphicsActivation activation;
    } JSGraphicContext;

    typedef struct JsGraphicFunction
    {
        const char *name;
        uint8_t length;
        JSCFunctionData *callback;
    } JsGraphicFunction;

    int js_graphic_register_function(JSContext *ctx, JSValue obj,
                                     const JsGraphicFunction *definition,
                                     JSGraphicContext *graphic_ctx);

    JSGraphicContext *js_graphic_context(JSContext *ctx, JSValueConst *func_data);

    JSGraphicContext *js_graphic_init(JSRuntimeContext *ctx);

    void js_graphic_destroy(JSGraphicContext *ctx);

    void js_graphic_set_frame_context(JSGraphicContext *graphic_ctx, SkiaCanvas *canvas,
                                      InputState *input, Window *window, int width, int height,
                                      float display_density);

    bool js_graphic_call_animation(JSGraphicContext *ctx, double timestamp);

    bool js_graphic_has_animation(JSGraphicContext *ctx);
    
    BudoAnimationWait *js_graphic_animation_wait(JSGraphicContext *graphic_ctx);

#ifdef __cplusplus
}
#endif

#endif