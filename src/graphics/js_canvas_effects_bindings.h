#ifndef BUDO_JS_CANVAS_EFFECTS_BINDINGS_H
#define BUDO_JS_CANVAS_EFFECTS_BINDINGS_H

#include "graphics/js_canvas_bindings.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void js_canvas_effects_register(JSContext *ctx, JSValueConst sys_obj, JSGraphicContext *graphic_ctx);
    
    void js_canvas_texture_effects_register(JSContext *ctx, JSValueConst canvas_obj, JSGraphicContext *graphic_ctx);
    
    SkiaDrawingDesk *js_canvas_texture_drawing_desk(JSContext *ctx, JSGraphicContext *graphic_ctx, JSValueConst this_val);

#ifdef __cplusplus
}
#endif

#endif