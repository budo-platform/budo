#ifndef JS_DEVICE_BINDINGS_H
#define JS_DEVICE_BINDINGS_H

#include "device_service.h"
#include "quickjs.h"

#ifdef __cplusplus
extern "C"
{
#endif

    DeviceContext *js_device_init(JSContext *ctx);

#ifdef __cplusplus
}
#endif

#endif