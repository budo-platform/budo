#ifndef BUDO_JS_IMAGE_BINDINGS_H
#define BUDO_JS_IMAGE_BINDINGS_H

#include "graphics/js_canvas_bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

JSValue js_image_load(JSContext *ctx, JSGraphicContext *graphics, int argc, JSValue *argv, bool from_buffer);
JSValue js_image_cancel(JSContext *ctx, JSGraphicContext *graphics, int argc, JSValue *argv);
void js_image_poll(JSGraphicContext *graphics);
void js_image_cleanup(JSGraphicContext *graphics);
bool js_image_pending(JSGraphicContext *graphics);

#ifdef __cplusplus
}
#endif

#endif