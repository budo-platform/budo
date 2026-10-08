#include "accessibility/js_accessibility_bindings.h"
#include "accessibility/accessibility_service.h"

#include <stdlib.h>
#include <string.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#endif

static JSValue js_accessibility_is_available(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val, (void)argc, (void)argv;
    return JS_NewBool(ctx, accessibility_available());
}

static JSValue js_accessibility_is_active(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val, (void)argc, (void)argv;
    return JS_NewBool(ctx, accessibility_active());
}

static void read_string(JSContext *ctx, JSValueConst object, const char *key, char *dst, size_t size)
{
    JSValue value = JS_GetPropertyStr(ctx, object, key);
    dst[0] = '\0';
    if (!JS_IsUndefined(value) && !JS_IsNull(value))
    {
        const char *text = JS_ToCString(ctx, value);
        if (text)
        {
            size_t length = strlen(text);
            if (length >= size)
                length = size - 1;
            memcpy(dst, text, length);
            dst[length] = '\0';
            JS_FreeCString(ctx, text);
        }
    }
    JS_FreeValue(ctx, value);
}

static bool read_number(JSContext *ctx, JSValueConst object, const char *key, float *out)
{
    JSValue value = JS_GetPropertyStr(ctx, object, key);
    double number = 0.0;
    bool present = JS_IsNumber(value) && JS_ToFloat64(ctx, &number, value) == 0;
    if (present)
        *out = (float)number;
    JS_FreeValue(ctx, value);
    return present;
}

static int read_flag(JSContext *ctx, JSValueConst object, const char *key)
{
    JSValue value = JS_GetPropertyStr(ctx, object, key);
    int flag = JS_IsUndefined(value) || JS_IsNull(value) ? -1 : JS_ToBool(ctx, value) ? 1 : 0;
    JS_FreeValue(ctx, value);
    return flag;
}

static JSValue js_accessibility_update(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    uint32_t count = 0;
    JSValue length = argc >= 1 ? JS_GetPropertyStr(ctx, argv[0], "length") : JS_UNDEFINED;
    int failed = argc < 1 || !BUDO_JS_IS_ARRAY(ctx, argv[0]) || JS_ToUint32(ctx, &count, length) < 0;
    JS_FreeValue(ctx, length);
    if (failed || count > ACCESSIBILITY_MAX_NODES)
        return JS_ThrowTypeError(ctx, "sys.accessibility.update(nodes): nodes must be an array of at most %d objects",
                                 ACCESSIBILITY_MAX_NODES);
    AccessibilityNode *nodes = count ? calloc(count, sizeof(AccessibilityNode)) : NULL;
    if (count && !nodes)
        return JS_ThrowOutOfMemory(ctx);
    for (uint32_t i = 0; i < count; i++)
    {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        AccessibilityNode *node = &nodes[i];
        if (!JS_IsObject(item))
        {
            JS_FreeValue(ctx, item);
            free(nodes);
            return JS_ThrowTypeError(ctx, "sys.accessibility.update: node %u is not an object", (unsigned)i);
        }
        read_string(ctx, item, "id", node->id, sizeof(node->id));
        read_string(ctx, item, "role", node->role, sizeof(node->role));
        if (!node->role[0])
            strcpy(node->role, "group");
        read_string(ctx, item, "label", node->label, sizeof(node->label));
        read_string(ctx, item, "value", node->value, sizeof(node->value));
        read_number(ctx, item, "x", &node->x);
        read_number(ctx, item, "y", &node->y);
        read_number(ctx, item, "width", &node->width);
        read_number(ctx, item, "height", &node->height);
        node->checked = (int8_t)read_flag(ctx, item, "checked");
        node->selected = read_flag(ctx, item, "selected") == 1;
        node->focused = read_flag(ctx, item, "focused") == 1;
        node->disabled = read_flag(ctx, item, "disabled") == 1;
        node->expanded = read_flag(ctx, item, "expanded") == 1;
        node->range_max = 1.0f;
        node->has_range = read_number(ctx, item, "max", &node->range_max);
        if (node->has_range)
        {
            read_number(ctx, item, "min", &node->range_min);
            read_number(ctx, item, "rangeValue", &node->range_value);
        }
        JS_FreeValue(ctx, item);
    }
    bool ok = accessibility_update(nodes, (int)count);
    free(nodes);
    return JS_NewBool(ctx, ok);
}

static JSValue js_accessibility_take_actions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val, (void)argc, (void)argv;
    AccessibilityAction actions[ACCESSIBILITY_MAX_ACTIONS];
    int count = accessibility_take_actions(actions, ACCESSIBILITY_MAX_ACTIONS);
    JSValue array = JS_NewArray(ctx);
    for (int i = 0; i < count; i++)
    {
        JSValue entry = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, entry, "id", JS_NewString(ctx, actions[i].id));
        JS_SetPropertyStr(ctx, entry, "action", JS_NewString(ctx, actions[i].action));
        JS_SetPropertyStr(ctx, entry, "value", JS_NewString(ctx, actions[i].value));
        JS_SetPropertyUint32(ctx, array, (uint32_t)i, entry);
    }
    return array;
}

static const JSCFunctionListEntry js_accessibility_funcs[] = {
    JS_CFUNC_DEF("isAvailable", 0, js_accessibility_is_available),
    JS_CFUNC_DEF("isActive", 0, js_accessibility_is_active),
    JS_CFUNC_DEF("update", 1, js_accessibility_update),
    JS_CFUNC_DEF("takeActions", 0, js_accessibility_take_actions),
};

void js_accessibility_init(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys = JS_GetPropertyStr(ctx, global, "sys");
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, object, js_accessibility_funcs,
                               sizeof(js_accessibility_funcs) / sizeof(js_accessibility_funcs[0]));
    JS_SetPropertyStr(ctx, sys, "accessibility", object);
    JS_FreeValue(ctx, sys);
    JS_FreeValue(ctx, global);
}