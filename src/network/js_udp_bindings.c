#include "js_udp_bindings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_UDP_CALLBACKS UDP_MAX_SOCKETS

struct JsUdpContext
{
    UdpContext *udp_ctx;
    JSContext *js_ctx;
    JSValue callbacks[MAX_UDP_CALLBACKS];
    bool callbacks_active[MAX_UDP_CALLBACKS];
};

static JsUdpContext *js_udp_ctx(JSContext *ctx,
                                JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    JsUdpContext *state = NULL;

    if (data && size == sizeof(state))
        memcpy(&state, data, sizeof(state));
    return state;
}

static JSValue js_udp_bind(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv, int magic,
                           JSValueConst *func_data)
{
    JsUdpContext *state = js_udp_ctx(ctx, func_data);
    (void)this_val;
    (void)magic;
    if (!state || !state->udp_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    int port;
    JS_ToInt32(ctx, &port, argv[0]);

    int handle = udp_bind(state->udp_ctx, port);
    return JS_NewInt32(ctx, handle);
}

static JSValue js_udp_get_port(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv, int magic,
                               JSValueConst *func_data)
{
    JsUdpContext *state = js_udp_ctx(ctx, func_data);
    (void)this_val;
    (void)magic;
    if (!state || !state->udp_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    return JS_NewInt32(ctx, udp_get_port(state->udp_ctx, handle));
}

static JSValue js_udp_send(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv, int magic,
                           JSValueConst *func_data)
{
    JsUdpContext *state = js_udp_ctx(ctx, func_data);
    (void)this_val;
    (void)magic;
    if (!state || !state->udp_ctx || argc < 4)
        return JS_FALSE;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    const char *host = JS_ToCString(ctx, argv[1]);
    if (!host)
        return JS_FALSE;

    int port;
    JS_ToInt32(ctx, &port, argv[2]);

    JSValue arr = argv[3];
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int len;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    if (len <= 0 || len > UDP_MAX_PACKET)
    {
        JS_FreeCString(ctx, host);
        return JS_FALSE;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf)
    {
        JS_FreeCString(ctx, host);
        return JS_FALSE;
    }

    for (int i = 0; i < len; i++)
    {
        JSValue elem = JS_GetPropertyUint32(ctx, arr, i);
        int val;
        JS_ToInt32(ctx, &val, elem);
        JS_FreeValue(ctx, elem);
        buf[i] = (uint8_t)(val & 0xFF);
    }

    bool result = udp_send(state->udp_ctx, handle, host, port, buf, len);

    free(buf);
    JS_FreeCString(ctx, host);

    return JS_NewBool(ctx, result);
}

static JSValue js_udp_on_message(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv, int magic,
                                 JSValueConst *func_data)
{
    JsUdpContext *state = js_udp_ctx(ctx, func_data);
    (void)this_val;
    (void)magic;
    if (!state || argc < 2)
        return JS_UNDEFINED;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    if (handle < 0 || handle >= MAX_UDP_CALLBACKS)
        return JS_UNDEFINED;

    if (state->callbacks_active[handle])
    {
        JS_FreeValue(ctx, state->callbacks[handle]);
        state->callbacks[handle] = JS_UNDEFINED;
        state->callbacks_active[handle] = false;
    }

    if (JS_IsFunction(ctx, argv[1]))
    {
        state->callbacks[handle] = JS_DupValue(ctx, argv[1]);
        state->callbacks_active[handle] = true;
    }

    return JS_UNDEFINED;
}

static JSValue js_udp_close_socket(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv, int magic,
                                   JSValueConst *func_data)
{
    JsUdpContext *state = js_udp_ctx(ctx, func_data);
    (void)this_val;
    (void)magic;
    if (!state || !state->udp_ctx || argc < 1)
        return JS_UNDEFINED;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    if (handle >= 0 && handle < MAX_UDP_CALLBACKS && state->callbacks_active[handle])
    {
        JS_FreeValue(ctx, state->callbacks[handle]);
        state->callbacks[handle] = JS_UNDEFINED;
        state->callbacks_active[handle] = false;
    }

    udp_close(state->udp_ctx, handle);
    return JS_UNDEFINED;
}

typedef struct JsUdpFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsUdpFunction;

static const JsUdpFunction js_udp_funcs[] = {
    {"bind", 1, js_udp_bind},
    {"getPort", 1, js_udp_get_port},
    {"send", 4, js_udp_send},
    {"onMessage", 2, js_udp_on_message},
    {"close", 1, js_udp_close_socket},
};

static int js_udp_add_function(JSContext *ctx, JSValue udp_obj,
                               const JsUdpFunction *definition,
                               JsUdpContext *state)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&state,
                                         sizeof(state));
    if (JS_IsException(data))
        return -1;

    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;

    return JS_SetPropertyStr(ctx, udp_obj, definition->name, function);
}

JsUdpContext *js_udp_init(JSContext *ctx)
{
    JsUdpContext *state = (JsUdpContext *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->js_ctx = ctx;

    for (int i = 0; i < MAX_UDP_CALLBACKS; i++)
    {
        state->callbacks[i] = JS_UNDEFINED;
    }

    state->udp_ctx = udp_create();
    if (!state->udp_ctx)
    {
        fprintf(stderr, "Warning: Failed to create UDP context\n");
        free(state);
        return NULL;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");
    JSValue network_obj = JS_GetPropertyStr(ctx, sys_obj, "network");

    JSValue udp_obj = JS_NewObject(ctx);
    for (size_t index = 0;
         index < sizeof(js_udp_funcs) / sizeof(js_udp_funcs[0]); index++)
    {
        if (js_udp_add_function(ctx, udp_obj, &js_udp_funcs[index], state) < 0)
        {
            JS_FreeValue(ctx, udp_obj);
            JS_FreeValue(ctx, network_obj);
            JS_FreeValue(ctx, sys_obj);
            JS_FreeValue(ctx, global);
            udp_destroy(state->udp_ctx);
            free(state);
            return NULL;
        }
    }

    JS_SetPropertyStr(ctx, network_obj, "udp", udp_obj);
    JS_FreeValue(ctx, network_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    return state;
}

UdpContext *js_udp_context(JsUdpContext *state)
{
    return state ? state->udp_ctx : NULL;
}

void js_udp_cleanup(JsUdpContext *state)
{
    if (!state)
        return;

    if (state->js_ctx)
    {
        for (int i = 0; i < MAX_UDP_CALLBACKS; i++)
        {
            if (state->callbacks_active[i])
            {
                JS_FreeValue(state->js_ctx, state->callbacks[i]);
                state->callbacks[i] = JS_UNDEFINED;
                state->callbacks_active[i] = false;
            }
        }
    }

    state->js_ctx = NULL;
    udp_destroy(state->udp_ctx);
    state->udp_ctx = NULL;
    free(state);
}

void js_udp_poll(JsUdpContext *state)
{
    if (!state || !state->js_ctx || !state->udp_ctx)
        return;

    uint8_t buf[UDP_MAX_PACKET];
    UdpDatagram dgram;

    for (int handle = 0; handle < MAX_UDP_CALLBACKS; handle++)
    {
        if (!state->callbacks_active[handle])
            continue;

        for (int n = 0; n < 64; n++)
        {
            int received = udp_recv(state->udp_ctx, handle, buf, sizeof(buf), &dgram);
            if (received <= 0)
                break;

            JSValue msg_obj = JS_NewObject(state->js_ctx);

            JSValue data_arr = JS_NewArray(state->js_ctx);
            for (int i = 0; i < received; i++)
            {
                JS_SetPropertyUint32(state->js_ctx, data_arr, i, JS_NewInt32(state->js_ctx, buf[i]));
            }
            JS_SetPropertyStr(state->js_ctx, msg_obj, "data", data_arr);
            JS_SetPropertyStr(state->js_ctx, msg_obj, "host", JS_NewString(state->js_ctx, dgram.host));
            JS_SetPropertyStr(state->js_ctx, msg_obj, "port", JS_NewInt32(state->js_ctx, dgram.port));

            JSValue args[1] = {msg_obj};
            JSValue ret = JS_Call(state->js_ctx, state->callbacks[handle], JS_UNDEFINED, 1, args);
            JS_FreeValue(state->js_ctx, ret);
            JS_FreeValue(state->js_ctx, msg_obj);
        }
    }
}