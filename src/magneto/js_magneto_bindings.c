#include "js_magneto_bindings.h"
#include "magneto_service.h"
#include <stdio.h>
#include <string.h>

static MagnetoContext *js_magneto_context(JSContext *ctx,
                                          JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    MagnetoContext *magneto_ctx = NULL;

    if (data && size == sizeof(magneto_ctx))
        memcpy(&magneto_ctx, data, sizeof(magneto_ctx));
    return magneto_ctx;
}

static JSValue js_magneto_is_available(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv, int magic,
                                       JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_FALSE;
    return JS_NewBool(ctx, magneto_is_available(magneto_ctx));
}

static JSValue js_magneto_has_accelerometer(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv, int magic,
                                            JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_FALSE;
    return JS_NewBool(ctx, magneto_has_accelerometer(magneto_ctx));
}

static JSValue js_magneto_has_compass(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_FALSE;
    return JS_NewBool(ctx, magneto_has_compass(magneto_ctx));
}

static JSValue js_magneto_start(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv, int magic,
                                JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_FALSE;
    ApiError error;
    return JS_NewBool(ctx, magneto_service_start(magneto_ctx, &error));
}

static JSValue js_magneto_stop_fn(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic,
                                  JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (magneto_ctx)
        magneto_stop(magneto_ctx);
    return JS_UNDEFINED;
}

static JSValue js_magneto_is_active(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic,
                                    JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_FALSE;
    return JS_NewBool(ctx, magneto_is_active(magneto_ctx));
}

static JSValue js_magneto_get_accel(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic,
                                    JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_NULL;

    MagnetoAccelData data;
    if (!magneto_get_accel(magneto_ctx, &data))
        return JS_NULL;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, data.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, data.y));
    JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, data.z));
    return obj;
}

static JSValue js_magneto_get_compass(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    MagnetoContext *magneto_ctx = js_magneto_context(ctx, func_data);
    if (!magneto_ctx)
        return JS_NULL;

    MagnetoCompassData data;
    if (!magneto_get_compass(magneto_ctx, &data))
        return JS_NULL;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, data.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, data.y));
    JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, data.z));
    JS_SetPropertyStr(ctx, obj, "heading", JS_NewFloat64(ctx, data.heading));
    return obj;
}

typedef struct JsMagnetoFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsMagnetoFunction;

static const JsMagnetoFunction js_magneto_funcs[] = {
    {"isAvailable", 0, js_magneto_is_available},
    {"hasAccelerometer", 0, js_magneto_has_accelerometer},
    {"hasCompass", 0, js_magneto_has_compass},
    {"start", 0, js_magneto_start},
    {"stop", 0, js_magneto_stop_fn},
    {"isActive", 0, js_magneto_is_active},
    {"getAccel", 0, js_magneto_get_accel},
    {"getCompass", 0, js_magneto_get_compass},
};

static int js_magneto_add_function(JSContext *ctx, JSValue magneto_obj,
                                   const JsMagnetoFunction *definition,
                                   MagnetoContext *magneto_ctx)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&magneto_ctx,
                                         sizeof(magneto_ctx));
    if (JS_IsException(data))
        return -1;

    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;

    return JS_SetPropertyStr(ctx, magneto_obj, definition->name, function);
}

MagnetoContext *js_magneto_init(JSContext *ctx)
{
    MagnetoContext *magneto_ctx = magneto_create();
    if (!magneto_ctx)
    {
        fprintf(stderr, "Failed to create magneto context\n");
        return NULL;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");

    JSValue magneto_obj = JS_NewObject(ctx);
    for (size_t index = 0;
         index < sizeof(js_magneto_funcs) / sizeof(js_magneto_funcs[0]); index++)
    {
        if (js_magneto_add_function(ctx, magneto_obj,
                                    &js_magneto_funcs[index], magneto_ctx) < 0)
        {
            JS_FreeValue(ctx, magneto_obj);
            JS_FreeValue(ctx, sys_obj);
            JS_FreeValue(ctx, global);
            magneto_destroy(magneto_ctx);
            return NULL;
        }
    }

    JS_SetPropertyStr(ctx, sys_obj, "sensors", magneto_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    return magneto_ctx;
}

void js_magneto_cleanup(MagnetoContext *magneto_ctx)
{
    if (magneto_ctx)
        magneto_destroy(magneto_ctx);
}