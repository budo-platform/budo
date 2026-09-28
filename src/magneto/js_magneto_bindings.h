#ifndef JS_MAGNETO_BINDINGS_H
#define JS_MAGNETO_BINDINGS_H

#include "quickjs.h"
#include "magneto_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    MagnetoContext *js_magneto_init(JSContext *ctx);

    void js_magneto_cleanup(MagnetoContext *magneto_ctx);

#ifdef __cplusplus
}
#endif

#endif