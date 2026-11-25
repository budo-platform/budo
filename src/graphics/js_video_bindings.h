#ifndef BUDO_JS_VIDEO_BINDINGS_H
#define BUDO_JS_VIDEO_BINDINGS_H

#include "graphics/js_canvas_bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

void js_video_register(JSContext *ctx, JSValueConst sys_obj,
                       JSGraphicContext *graphics);
void js_video_poll(JSGraphicContext *graphics);
void js_video_context_lost(JSGraphicContext *graphics);
void js_video_context_restored(JSGraphicContext *graphics);
void js_video_cleanup(JSGraphicContext *graphics);
bool js_video_pending(JSGraphicContext *graphics);

#ifdef __cplusplus
}
#endif

#endif