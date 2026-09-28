#ifndef BUDO_JS_GL_BINDINGS_H
#define BUDO_JS_GL_BINDINGS_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void js_gl_register(JSContext *context, JSGraphicContext *graphic_ctx, JSValue sys_object);

#ifdef __cplusplus
}
#endif

#endif