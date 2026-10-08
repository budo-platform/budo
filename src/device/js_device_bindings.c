#include "js_device_bindings.h"
#include "device_service.h"

#include <stdlib.h>
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

static JSValue js_device_set_clipboard_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv,
                                            int magic, JSValue *func_data)
{
    (void)this_val, (void)magic, (void)func_data;
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "sys.device.setClipboardText(text): text must be a string");
    const char *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    bool ok = device_set_clipboard_text(text);
    JS_FreeCString(ctx, text);
    return JS_NewBool(ctx, ok);
}

static JSValue js_device_get_clipboard_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv,
                                            int magic, JSValue *func_data)
{
    (void)this_val, (void)argc, (void)argv, (void)magic, (void)func_data;
    char *text = device_get_clipboard_text();
    JSValue result = text ? JS_NewString(ctx, text) : JS_NULL;
    free(text);
    return result;
}

static JSValue js_device_haptic(JSContext *ctx, JSValue this_val, int argc, JSValue *argv,
                                int magic, JSValue *func_data)
{
    (void)this_val, (void)magic, (void)func_data;
    DeviceHaptic kind = DEVICE_HAPTIC_LIGHT;
    if (argc >= 1 && !JS_IsUndefined(argv[0]))
    {
        const char *name = JS_ToCString(ctx, argv[0]);
        bool known = name && device_haptic_from_name(name, &kind);
        JSValue error = known ? JS_UNDEFINED : JS_ThrowRangeError(ctx, "sys.device.haptic: unknown kind '%s'", name ? name : "");
        JS_FreeCString(ctx, name);
        if (!known)
            return error;
    }
    return JS_NewBool(ctx, device_haptic(kind));
}

static JSValue js_device_get_preferences(JSContext *ctx, JSValue this_val, int argc, JSValue *argv,
                                         int magic, JSValue *func_data)
{
    (void)this_val, (void)argc, (void)argv, (void)magic, (void)func_data;
    DevicePreferences preferences;
    device_get_preferences(&preferences);
    JSValue result = JS_NewObject(ctx);
    JSValue safe = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "darkMode", JS_NewBool(ctx, preferences.dark_mode));
    JS_SetPropertyStr(ctx, result, "reducedMotion", JS_NewBool(ctx, preferences.reduced_motion));
    JS_SetPropertyStr(ctx, result, "highContrast", JS_NewBool(ctx, preferences.high_contrast));
    JS_SetPropertyStr(ctx, result, "fontScale", JS_NewFloat64(ctx, preferences.font_scale));
    JS_SetPropertyStr(ctx, safe, "top", JS_NewFloat64(ctx, preferences.safe_top));
    JS_SetPropertyStr(ctx, safe, "right", JS_NewFloat64(ctx, preferences.safe_right));
    JS_SetPropertyStr(ctx, safe, "bottom", JS_NewFloat64(ctx, preferences.safe_bottom));
    JS_SetPropertyStr(ctx, safe, "left", JS_NewFloat64(ctx, preferences.safe_left));
    JS_SetPropertyStr(ctx, result, "safeArea", safe);
    JS_SetPropertyStr(ctx, result, "keyboardInset", JS_NewFloat64(ctx, preferences.keyboard_inset));
    return result;
}

static JSValue js_device_set_cursor(JSContext *ctx, JSValue this_val, int argc, JSValue *argv,
                                    int magic, JSValue *func_data)
{
    (void)this_val, (void)magic, (void)func_data;
    DeviceCursor cursor = DEVICE_CURSOR_DEFAULT;
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
    {
        const char *name = JS_ToCString(ctx, argv[0]);
        bool known = name && device_cursor_from_name(name, &cursor);
        JSValue error = known ? JS_UNDEFINED : JS_ThrowRangeError(ctx, "sys.device.setCursor: unknown cursor '%s'", name ? name : "");
        JS_FreeCString(ctx, name);
        if (!known)
            return error;
    }
    return JS_NewBool(ctx, device_set_cursor(cursor));
}

static int js_device_add(JSContext *ctx, JSValue device_obj, const char *name, JSCFunctionData *function,
                         int length, DeviceContext *state)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&state, sizeof(state));
    JSValue value = JS_NewCFunctionData(ctx, function, length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(value))
        return -1;
    return JS_SetPropertyStr(ctx, device_obj, name, value);
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

    if (js_device_add(ctx, device_obj, "setClipboardText", js_device_set_clipboard_text, 1, state) < 0 ||
        js_device_add(ctx, device_obj, "getClipboardText", js_device_get_clipboard_text, 0, state) < 0 ||
        js_device_add(ctx, device_obj, "haptic", js_device_haptic, 1, state) < 0 ||
        js_device_add(ctx, device_obj, "getPreferences", js_device_get_preferences, 0, state) < 0 ||
        js_device_add(ctx, device_obj, "setCursor", js_device_set_cursor, 1, state) < 0)
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