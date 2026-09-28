#ifndef BUDO_JS_CAPABILITIES_BINDINGS_H
#define BUDO_JS_CAPABILITIES_BINDINGS_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void js_capabilities_init(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif