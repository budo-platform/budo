#include "js_network_bindings.h"
#include "network_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#define BUDO_JS_NEW_CLASS_ID(rt, id) JS_NewClassID(rt, id)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#define BUDO_JS_NEW_CLASS_ID(rt, id) JS_NewClassID(id)
#endif

#define MAX_PENDING_FETCHES 64

typedef struct JsFetchPromise JsFetchPromise;

struct JsNetworkContext
{
    NetworkContext *network_ctx;
    JSContext *js_ctx;
    JSValue function_data;
    JSClassID response_class_id;
    JSClassID headers_class_id;
    uint32_t generation;
    bool shutting_down;
    JsFetchPromise *pending;
};

typedef struct
{
    NetworkHeader *headers;
    int count;
} JSHeadersData;

static void js_headers_finalizer(JSRuntime *rt, JSValue val)
{
    JSHeadersData *data = JS_GetOpaque(val, JS_GetClassID(val));
    if (data)
    {
        for (int i = 0; i < data->count; i++)
        {
            js_free_rt(rt, data->headers[i].name);
            js_free_rt(rt, data->headers[i].value);
        }
        js_free_rt(rt, data->headers);
        js_free_rt(rt, data);
    }
}

static const JSClassDef js_headers_class = {
    "Headers",
    .finalizer = js_headers_finalizer,
};

static JSHeadersData *create_headers_data(JSContext *ctx, NetworkHeader *src, int count)
{
    JSHeadersData *data = js_malloc(ctx, sizeof(JSHeadersData));
    if (!data)
        return NULL;

    data->count = count;
    data->headers = js_malloc(ctx, sizeof(NetworkHeader) * (count > 0 ? count : 1));
    if (!data->headers)
    {
        js_free(ctx, data);
        return NULL;
    }

    for (int i = 0; i < count; i++)
    {
        data->headers[i].name = js_strdup(ctx, src[i].name);
        data->headers[i].value = js_strdup(ctx, src[i].value);
        if (!data->headers[i].name || !data->headers[i].value)
        {
            for (int j = 0; j <= i; j++)
            {
                js_free(ctx, data->headers[j].name);
                js_free(ctx, data->headers[j].value);
            }
            js_free(ctx, data->headers);
            js_free(ctx, data);
            return NULL;
        }
    }

    return data;
}

static JSValue js_headers_get(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv, int magic)
{
    JSHeadersData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data || argc < 1)
        return JS_NULL;

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;

    JSValue result = JS_NULL;
    for (int i = 0; i < data->count; i++)
    {
        if (strcasecmp(data->headers[i].name, name) == 0)
        {
            result = JS_NewString(ctx, data->headers[i].value);
            break;
        }
    }

    JS_FreeCString(ctx, name);
    return result;
}

static JSValue js_headers_has(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv, int magic)
{
    JSHeadersData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data || argc < 1)
        return JS_FALSE;

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;

    bool found = false;
    for (int i = 0; i < data->count; i++)
    {
        if (strcasecmp(data->headers[i].name, name) == 0)
        {
            found = true;
            break;
        }
    }

    JS_FreeCString(ctx, name);
    return JS_NewBool(ctx, found);
}

static JSValue js_headers_entries(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv, int magic)
{
    (void)argc;
    (void)argv;
    JSHeadersData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data)
        return JS_NewArray(ctx);

    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < data->count; i++)
    {
        JSValue pair = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, pair, 0, JS_NewString(ctx, data->headers[i].name));
        JS_SetPropertyUint32(ctx, pair, 1, JS_NewString(ctx, data->headers[i].value));
        JS_SetPropertyUint32(ctx, arr, i, pair);
    }
    return arr;
}

static JSValue js_headers_keys(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv, int magic)
{
    (void)argc;
    (void)argv;
    JSHeadersData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data)
        return JS_NewArray(ctx);

    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < data->count; i++)
    {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, data->headers[i].name));
    }
    return arr;
}

static JSValue js_headers_values(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv, int magic)
{
    (void)argc;
    (void)argv;
    JSHeadersData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data)
        return JS_NewArray(ctx);

    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < data->count; i++)
    {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, data->headers[i].value));
    }
    return arr;
}

static JSValue js_headers_forEach(JSContext *ctx, JSValueConst this_val, int argc,
                                  JSValueConst *argv, int magic)
{
    JSHeadersData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data || argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_UNDEFINED;

    for (int i = 0; i < data->count; i++)
    {
        JSValue args[2] = {
            JS_NewString(ctx, data->headers[i].value),
            JS_NewString(ctx, data->headers[i].name)};
        JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
        if (JS_IsException(ret))
        {
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, ret);
    }
    return JS_UNDEFINED;
}

static JSValue create_headers_object(JsNetworkContext *state,
                                     NetworkHeader *headers, int count)
{
    JSContext *ctx = state->js_ctx;
    JSValue obj = JS_NewObjectClass(ctx, state->headers_class_id);
    if (JS_IsException(obj))
        return obj;

    JSHeadersData *data = create_headers_data(ctx, headers, count);
    if (!data)
    {
        JS_FreeValue(ctx, obj);
        return JS_ThrowInternalError(ctx, "Failed to create Headers");
    }

    JS_SetOpaque(obj, data);

    JS_SetPropertyStr(ctx, obj, "get",
                      JS_NewCFunctionMagic(ctx, js_headers_get, "get", 1,
                                           JS_CFUNC_generic_magic,
                                           (int)state->headers_class_id));
    JS_SetPropertyStr(ctx, obj, "has",
                      JS_NewCFunctionMagic(ctx, js_headers_has, "has", 1,
                                           JS_CFUNC_generic_magic,
                                           (int)state->headers_class_id));
    JS_SetPropertyStr(ctx, obj, "entries",
                      JS_NewCFunctionMagic(ctx, js_headers_entries, "entries", 0,
                                           JS_CFUNC_generic_magic,
                                           (int)state->headers_class_id));
    JS_SetPropertyStr(ctx, obj, "keys",
                      JS_NewCFunctionMagic(ctx, js_headers_keys, "keys", 0,
                                           JS_CFUNC_generic_magic,
                                           (int)state->headers_class_id));
    JS_SetPropertyStr(ctx, obj, "values",
                      JS_NewCFunctionMagic(ctx, js_headers_values, "values", 0,
                                           JS_CFUNC_generic_magic,
                                           (int)state->headers_class_id));
    JS_SetPropertyStr(ctx, obj, "forEach",
                      JS_NewCFunctionMagic(ctx, js_headers_forEach, "forEach", 1,
                                           JS_CFUNC_generic_magic,
                                           (int)state->headers_class_id));

    return obj;
}

typedef struct
{
    int status;
    char *status_text;
    char *url;
    bool redirected;
    bool ok;
    uint8_t *body;
    size_t body_len;
    bool body_used;
    NetworkHeader *headers;
    int header_count;
} JSResponseData;

static void js_response_finalizer(JSRuntime *rt, JSValue val)
{
    JSResponseData *data = JS_GetOpaque(val, JS_GetClassID(val));
    if (data)
    {
        js_free_rt(rt, data->status_text);
        js_free_rt(rt, data->url);
        js_free_rt(rt, data->body);
        for (int i = 0; i < data->header_count; i++)
        {
            js_free_rt(rt, data->headers[i].name);
            js_free_rt(rt, data->headers[i].value);
        }
        js_free_rt(rt, data->headers);
        js_free_rt(rt, data);
    }
}

static const JSClassDef js_response_class = {
    "Response",
    .finalizer = js_response_finalizer,
};

static JSValue js_response_text(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv, int magic)
{
    (void)argc;
    (void)argv;
    JSResponseData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data)
        return JS_ThrowTypeError(ctx, "Not a Response object");

    if (data->body_used)
        return JS_ThrowTypeError(ctx, "Body has already been consumed");

    data->body_used = true;

    if (!data->body || data->body_len == 0)
        return JS_NewString(ctx, "");

    return JS_NewStringLen(ctx, (const char *)data->body, data->body_len);
}

static JSValue js_response_json(JSContext *ctx, JSValueConst this_val, int argc,
                                JSValueConst *argv, int magic)
{
    (void)argc;
    (void)argv;
    JSResponseData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data)
        return JS_ThrowTypeError(ctx, "Not a Response object");

    if (data->body_used)
        return JS_ThrowTypeError(ctx, "Body has already been consumed");

    data->body_used = true;

    if (!data->body || data->body_len == 0)
        return JS_ThrowSyntaxError(ctx, "Unexpected end of JSON input");

    JSValue str = JS_NewStringLen(ctx, (const char *)data->body, data->body_len);
    if (JS_IsException(str))
        return str;

    JSValue result = JS_ParseJSON(ctx, (const char *)data->body, data->body_len, "<response>");
    JS_FreeValue(ctx, str);
    return result;
}

static JSValue js_response_arrayBuffer(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv, int magic)
{
    (void)argc;
    (void)argv;
    JSResponseData *data = JS_GetOpaque2(ctx, this_val, (JSClassID)(uint32_t)magic);
    if (!data)
        return JS_ThrowTypeError(ctx, "Not a Response object");

    if (data->body_used)
        return JS_ThrowTypeError(ctx, "Body has already been consumed");

    data->body_used = true;

    if (!data->body || data->body_len == 0)
        return JS_NewArrayBufferCopy(ctx, NULL, 0);

    return JS_NewArrayBufferCopy(ctx, data->body, data->body_len);
}

static JSValue create_response_object(JsNetworkContext *state,
                                      NetworkResponse *resp)
{
    JSContext *ctx = state->js_ctx;
    JSValue obj = JS_NewObjectClass(ctx, state->response_class_id);
    if (JS_IsException(obj))
        return obj;

    JSResponseData *data = js_malloc(ctx, sizeof(JSResponseData));
    if (!data)
    {
        JS_FreeValue(ctx, obj);
        return JS_ThrowInternalError(ctx, "Out of memory");
    }
    memset(data, 0, sizeof(*data));

    data->status = resp->status;
    data->status_text = js_strdup(ctx, resp->status_text ? resp->status_text : "");
    data->url = js_strdup(ctx, resp->url ? resp->url : "");
    data->redirected = resp->redirected;
    data->ok = (resp->status >= 200 && resp->status < 300);
    data->body_used = false;

    data->body = js_malloc(ctx, resp->body_len > 0 ? resp->body_len : 1);
    if (data->body && resp->body)
        memcpy(data->body, resp->body, resp->body_len);
    data->body_len = resp->body_len;

    data->header_count = resp->header_count;
    data->headers = js_malloc(ctx, sizeof(NetworkHeader) * (resp->header_count > 0 ? resp->header_count : 1));
    JS_SetOpaque(obj, data);
    if (!data->status_text || !data->url || !data->body || !data->headers)
    {
        js_response_finalizer(JS_GetRuntime(ctx), obj);
        JS_SetOpaque(obj, NULL);
        JS_FreeValue(ctx, obj);
        return JS_ThrowInternalError(ctx, "Out of memory while creating Response");
    }
    memset(data->headers, 0, sizeof(NetworkHeader) * (resp->header_count > 0 ? resp->header_count : 1));
    for (int i = 0; i < resp->header_count; i++)
    {
        data->headers[i].name = js_strdup(ctx, resp->headers[i].name);
        data->headers[i].value = js_strdup(ctx, resp->headers[i].value);
        if (!data->headers[i].name || !data->headers[i].value)
        {
            data->header_count = i + 1;
            js_response_finalizer(JS_GetRuntime(ctx), obj);
            JS_SetOpaque(obj, NULL);
            JS_FreeValue(ctx, obj);
            return JS_ThrowInternalError(ctx, "Out of memory while creating Response headers");
        }
    }

    JS_SetPropertyStr(ctx, obj, "status", JS_NewInt32(ctx, data->status));
    JS_SetPropertyStr(ctx, obj, "statusText", JS_NewString(ctx, data->status_text));
    JS_SetPropertyStr(ctx, obj, "ok", JS_NewBool(ctx, data->ok));
    JS_SetPropertyStr(ctx, obj, "url", JS_NewString(ctx, data->url));
    JS_SetPropertyStr(ctx, obj, "redirected", JS_NewBool(ctx, data->redirected));
    JS_SetPropertyStr(ctx, obj, "bodyUsed", JS_NewBool(ctx, false));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, "basic"));

    JS_SetPropertyStr(ctx, obj, "headers",
                      create_headers_object(state, resp->headers, resp->header_count));

    JS_SetPropertyStr(ctx, obj, "text",
                      JS_NewCFunctionMagic(ctx, js_response_text, "text", 0,
                                           JS_CFUNC_generic_magic,
                                           (int)state->response_class_id));
    JS_SetPropertyStr(ctx, obj, "json",
                      JS_NewCFunctionMagic(ctx, js_response_json, "json", 0,
                                           JS_CFUNC_generic_magic,
                                           (int)state->response_class_id));
    JS_SetPropertyStr(ctx, obj, "arrayBuffer",
                      JS_NewCFunctionMagic(ctx, js_response_arrayBuffer,
                                           "arrayBuffer", 0,
                                           JS_CFUNC_generic_magic,
                                           (int)state->response_class_id));

    return obj;
}

struct JsFetchPromise
{
    JSValue resolve; 
    JSValue reject;  
    JSContext *ctx;
    JsNetworkContext *state;
    uint32_t generation;
    bool active;
};

static JsNetworkContext *js_network_binding_state(
    JSContext *ctx, JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);

    return data && size == sizeof(JsNetworkContext)
               ? (JsNetworkContext *)data
               : NULL;
}

static void js_network_ctx_free(JSRuntime *rt, void *opaque, void *ptr)
{
    JsNetworkContext *state = (JsNetworkContext *)ptr;
    (void)rt;
    (void)opaque;

    if (!state)
        return;
    network_shutdown(state->network_ctx);
    network_destroy(state->network_ctx);
    free(state->pending);
    free(state);
}

static void js_fetch_async_callback(int request_id, NetworkResponse *response,
                                    const char *error, void *user_data)
{
    (void)request_id;
    JsFetchPromise *prom = (JsFetchPromise *)user_data;
    JsNetworkContext *state = prom->state;
    if (!prom->active || !state || state->shutting_down ||
        state->generation != prom->generation || state->js_ctx != prom->ctx)
    {
        network_response_free(response);
        return;
    }
    JSContext *ctx = prom->ctx;

    if (response)
    {
        JSValue resp_obj = create_response_object(state, response);
        network_response_free(response);

        if (JS_IsException(resp_obj))
        {
            JSValue exc = JS_GetException(ctx);
            JS_Call(ctx, prom->reject, JS_UNDEFINED, 1, &exc);
            JS_FreeValue(ctx, exc);
        }
        else
        {
            JS_Call(ctx, prom->resolve, JS_UNDEFINED, 1, &resp_obj);
        }
        JS_FreeValue(ctx, resp_obj);
    }
    else
    {
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message",
                          JS_NewString(ctx, error ? error : "fetch failed"));
        JS_Call(ctx, prom->reject, JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
    }

    JS_FreeValue(ctx, prom->resolve);
    JS_FreeValue(ctx, prom->reject);
    prom->active = false;
}

static JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv, int magic,
                        JSValueConst *func_data)
{
    JsNetworkContext *state = js_network_binding_state(ctx, func_data);
    (void)this_val;
    (void)magic;

    if (!state || state->shutting_down || !state->network_ctx || state->js_ctx != ctx)
    {
        return JS_ThrowInternalError(ctx, "Network API not initialized");
    }

    if (!network_is_enabled(state->network_ctx))
    {
        return JS_ThrowInternalError(ctx,
                                     "Network access denied: no \"network\" policy in app.json");
    }

    if (argc < 1)
        return JS_ThrowTypeError(ctx, "fetch requires a URL argument");

    const char *url = JS_ToCString(ctx, argv[0]);
    if (!url)
        return JS_EXCEPTION;

    const char *method = "GET";
    const char *method_str = NULL;
    NetworkHeader req_headers[64];
    int req_header_count = 0;
    const uint8_t *body = NULL;
    size_t body_len = 0;
    const char *body_str = NULL;

    if (argc >= 2 && JS_IsObject(argv[1]))
    {
        JSValue opts = argv[1];

        JSValue method_val = JS_GetPropertyStr(ctx, opts, "method");
        if (JS_IsString(method_val))
        {
            method_str = JS_ToCString(ctx, method_val);
            if (method_str)
                method = method_str;
        }
        JS_FreeValue(ctx, method_val);

        JSValue headers_val = JS_GetPropertyStr(ctx, opts, "headers");
        if (JS_IsObject(headers_val))
        {
            
            JSPropertyEnum *props;
            uint32_t prop_count;
            if (JS_GetOwnPropertyNames(ctx, &props, &prop_count,
                                       headers_val, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0)
            {
                for (uint32_t i = 0; i < prop_count && req_header_count < 64; i++)
                {
                    JSValue key = JS_AtomToString(ctx, props[i].atom);
                    JSValue val = JS_GetProperty(ctx, headers_val, props[i].atom);

                    const char *key_str = JS_ToCString(ctx, key);
                    const char *val_str = JS_ToCString(ctx, val);

                    if (key_str && val_str)
                    {
                        req_headers[req_header_count].name = strdup(key_str);
                        req_headers[req_header_count].value = strdup(val_str);
                        req_header_count++;
                    }

                    if (key_str)
                        JS_FreeCString(ctx, key_str);
                    if (val_str)
                        JS_FreeCString(ctx, val_str);
                    JS_FreeValue(ctx, key);
                    JS_FreeValue(ctx, val);
                    JS_FreeAtom(ctx, props[i].atom);
                }
                js_free(ctx, props);
            }
        }
        JS_FreeValue(ctx, headers_val);

        JSValue body_val = JS_GetPropertyStr(ctx, opts, "body");
        if (JS_IsString(body_val))
        {
            body_str = JS_ToCString(ctx, body_val);
            if (body_str)
            {
                body = (const uint8_t *)body_str;
                body_len = strlen(body_str);
            }
        }
        else if (!JS_IsUndefined(body_val) && !JS_IsNull(body_val))
        {
            
            size_t ab_len;
            uint8_t *ab_data = JS_GetArrayBuffer(ctx, &ab_len, body_val);
            if (ab_data)
            {
                body = ab_data;
                body_len = ab_len;
            }
        }
        JS_FreeValue(ctx, body_val);
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise))
    {
        JS_FreeCString(ctx, url);
        if (method_str)
            JS_FreeCString(ctx, method_str);
        if (body_str)
            JS_FreeCString(ctx, body_str);
        for (int i = 0; i < req_header_count; i++)
        {
            free(req_headers[i].name);
            free(req_headers[i].value);
        }
        return JS_EXCEPTION;
    }

    JsFetchPromise *prom = NULL;
    for (int i = 0; i < MAX_PENDING_FETCHES; i++)
    {
        if (!state->pending[i].active)
        {
            prom = &state->pending[i];
            break;
        }
    }
    if (!prom)
    {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeValue(ctx, promise);
        JS_FreeCString(ctx, url);
        if (method_str)
            JS_FreeCString(ctx, method_str);
        if (body_str)
            JS_FreeCString(ctx, body_str);
        for (int i = 0; i < req_header_count; i++)
        {
            free(req_headers[i].name);
            free(req_headers[i].value);
        }
        return JS_ThrowInternalError(ctx, "Too many pending fetch requests");
    }

    prom->resolve = resolving_funcs[0];
    prom->reject = resolving_funcs[1];
    prom->ctx = ctx;
    prom->state = state;
    prom->generation = state->generation;
    prom->active = true;

    ApiError api_error;
    int req_id = network_service_request_async(
        state->network_ctx, method, url,
        (const NetworkHeader *)req_headers, req_header_count,
        body, body_len,
        js_fetch_async_callback, prom, &api_error);

    JS_FreeCString(ctx, url);
    if (method_str)
        JS_FreeCString(ctx, method_str);
    if (body_str)
        JS_FreeCString(ctx, body_str);
    for (int i = 0; i < req_header_count; i++)
    {
        free(req_headers[i].name);
        free(req_headers[i].value);
    }

    if (req_id < 0)
    {
        
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message",
                          JS_NewString(ctx, network_get_error(state->network_ctx)));
        JS_Call(ctx, prom->reject, JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
        JS_FreeValue(ctx, prom->resolve);
        JS_FreeValue(ctx, prom->reject);
        prom->active = false;
    }

    return promise;
}

JsNetworkContext *js_network_init(JSContext *ctx, const char *project_dir)
{
    JsNetworkContext *state =
        (JsNetworkContext *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->js_ctx = ctx;
    state->function_data = JS_UNDEFINED;
    state->generation = 1;
    state->pending =
        (JsFetchPromise *)calloc(MAX_PENDING_FETCHES, sizeof(*state->pending));
    if (!state->pending)
    {
        free(state);
        return NULL;
    }

    JSRuntime *rt = JS_GetRuntime(ctx);
    BUDO_JS_NEW_CLASS_ID(rt, &state->response_class_id);
    BUDO_JS_NEW_CLASS_ID(rt, &state->headers_class_id);
    if (JS_NewClass(rt, state->response_class_id, &js_response_class) < 0 ||
        JS_NewClass(rt, state->headers_class_id, &js_headers_class) < 0)
    {
        free(state->pending);
        free(state);
        return NULL;
    }

    NetworkPolicy policy;
    network_policy_load_app_json(&policy, project_dir);

    state->network_ctx = network_create(&policy);
    if (!state->network_ctx)
    {
        fprintf(stderr, "Failed to create network context\n");
        free(state->pending);
        free(state);
        return NULL;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");

    JSValue net_obj = JS_NewObject(ctx);

    state->function_data = JS_NewArrayBuffer(
        ctx, (uint8_t *)state, sizeof(*state), js_network_ctx_free,
        NULL, false);
    if (JS_IsException(state->function_data))
    {
        JS_FreeValue(ctx, net_obj);
        JS_FreeValue(ctx, sys_obj);
        JS_FreeValue(ctx, global);
        network_destroy(state->network_ctx);
        free(state->pending);
        free(state);
        return NULL;
    }
    JSValue fetch_func = JS_NewCFunctionData(
        ctx, js_fetch, 2, 0, 1, &state->function_data);
    if (JS_IsException(fetch_func))
    {
        JS_FreeValue(ctx, fetch_func);
        JS_FreeValue(ctx, net_obj);
        JS_FreeValue(ctx, sys_obj);
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, state->function_data);
        return NULL;
    }
    JS_SetPropertyStr(ctx, net_obj, "fetch", fetch_func);

    JS_SetPropertyStr(ctx, sys_obj, "network", net_obj);

    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    if (network_is_enabled(state->network_ctx))
    {
        if (policy.allow_all)
            printf("Network: enabled (all domains)\n");
        else
            printf("Network: enabled (%d domain(s))\n", policy.domain_count);
    }

    return state;
}

NetworkContext *js_network_context(JsNetworkContext *state)
{
    return state ? state->network_ctx : NULL;
}

void js_network_poll(JsNetworkContext *state)
{
    if (state && !state->shutting_down && state->network_ctx)
        network_async_poll(state->network_ctx);
}

void js_network_cleanup(JsNetworkContext *state)
{
    JSContext *ctx;
    JSValue function_data;

    if (!state || state->shutting_down)
        return;

    state->shutting_down = true;
    state->generation++;
    ctx = state->js_ctx;
    function_data = state->function_data;

    network_shutdown(state->network_ctx);
    if (ctx)
    {
        for (int i = 0; i < MAX_PENDING_FETCHES; i++)
        {
            JsFetchPromise *prom = &state->pending[i];
            if (!prom->active)
                continue;
            JS_FreeValue(ctx, prom->resolve);
            JS_FreeValue(ctx, prom->reject);
            prom->active = false;
        }
    }

    state->function_data = JS_UNDEFINED;
    state->js_ctx = NULL;
    network_destroy(state->network_ctx);
    state->network_ctx = NULL;
    free(state->pending);
    state->pending = NULL;

    if (ctx && !JS_IsUndefined(function_data))
        JS_FreeValue(ctx, function_data);
}