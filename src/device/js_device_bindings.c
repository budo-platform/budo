#include "js_device_bindings.h"
#include "device_service.h"

#include <string.h>

static DeviceContext *js_device_ctx(JSContext *ctx,
                                    JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    DeviceContext *state = NULL;

    if (data && size == sizeof(state))
        memcpy(&state, data, sizeof(state));
    return state;
}

static JSValue js_device_keep_screen_on(JSContext *ctx, JSValue this_val,
                                        int argc, JSValue *argv,
                                        int magic, JSValue *func_data)
{
    ApiError error;
    (void)this_val;
    (void)magic;
    bool enabled = false;
    if (argc >= 1)
        enabled = JS_ToBool(ctx, argv[0]) ? true : false;
    bool ok = device_binding_state_keep_screen_on(
        js_device_ctx(ctx, func_data), enabled, &error);
    return JS_NewBool(ctx, ok);
}

DeviceContext *js_device_init(JSContext *ctx)
{
    DeviceContext *state = device_binding_state_create();
    if (!state)
        return NULL;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");
    JSValue device_obj = JS_NewObject(ctx);
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&state,
                                         sizeof(state));
    JSValue function = JS_NewCFunctionData(
        ctx, js_device_keep_screen_on, 1, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function) ||
        JS_SetPropertyStr(ctx, device_obj, "keepScreenOn", function) < 0)
    {
        JS_FreeValue(ctx, device_obj);
        JS_FreeValue(ctx, sys_obj);
        JS_FreeValue(ctx, global);
        device_binding_state_destroy(state);
        return NULL;
    }

    JS_SetPropertyStr(ctx, sys_obj, "device", device_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);
    return state;
}