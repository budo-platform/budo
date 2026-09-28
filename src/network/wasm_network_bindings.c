#include "wasm_util/wasm_memory.h"
#include "wasm_network_bindings.h"
#include "wasm_network_security.h"
#include "network_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WASM_NET_MAX_PENDING 32

typedef struct WasmFetchCompletion WasmFetchCompletion;

typedef struct
{
    bool in_use;
    bool ready;                
    int async_id;              
    uint32_t generation;       
    NetworkResponse *response; 
    char *error;               
    char *headers_str;         
    size_t headers_str_len;
    WasmFetchCompletion *completion;
} WasmFetchSlot;

struct WasmFetchCompletion
{
    WasmNetworkBindingState *state;
    int slot_id;
    int async_id;
    uint32_t state_generation;
    uint32_t slot_generation;
    bool active;
};

struct WasmNetworkBindingState
{
    NetworkContext *network_ctx;
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool has_memory;
    bool shutting_down;
    uint32_t generation;
    uint32_t next_slot_generation;
    WasmFetchSlot slots[WASM_NET_MAX_PENDING];
    WasmFetchCompletion completions[NETWORK_MAX_ASYNC];
};

static const char wasm_network_scope_owner;

static int alloc_slot(WasmNetworkBindingState *state)
{
    if (!state || state->shutting_down)
        return -1;
    for (int i = 0; i < WASM_NET_MAX_PENDING; i++)
    {
        if (!state->slots[i].in_use)
        {
            WasmFetchSlot *slot = &state->slots[i];
            memset(slot, 0, sizeof(*slot));
            slot->in_use = true;
            slot->async_id = -1;
            slot->generation = state->next_slot_generation++ &
                               BUDO_WASM_NETWORK_GENERATION_MASK;
            if (slot->generation == 0)
                slot->generation = state->next_slot_generation++ &
                                   BUDO_WASM_NETWORK_GENERATION_MASK;
            return i;
        }
    }
    return -1;
}

static void free_slot(WasmNetworkBindingState *state, int id)
{
    if (!state || id < 0 || id >= WASM_NET_MAX_PENDING)
        return;
    WasmFetchSlot *s = &state->slots[id];
    if (s->response)
    {
        network_response_free(s->response);
        s->response = NULL;
    }
    if (s->error)
    {
        free(s->error);
        s->error = NULL;
    }
    if (s->headers_str)
    {
        free(s->headers_str);
        s->headers_str = NULL;
    }
    s->in_use = false;
    s->ready = false;
    s->async_id = -1;
    s->headers_str_len = 0;
    s->completion = NULL;
}

static WasmFetchSlot *get_slot(WasmNetworkBindingState *state, int id)
{
    if (!state || state->shutting_down || id < 0 || id >= WASM_NET_MAX_PENDING)
        return NULL;
    WasmFetchSlot *s = &state->slots[id];
    if (!s->in_use)
        return NULL;
    return s;
}

static WasmFetchCompletion *alloc_completion(WasmNetworkBindingState *state)
{
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        if (!state->completions[i].active)
        {
            memset(&state->completions[i], 0,
                   sizeof(state->completions[i]));
            state->completions[i].active = true;
            state->completions[i].async_id = -1;
            return &state->completions[i];
        }
    }
    return NULL;
}

static void ensure_headers_str(WasmFetchSlot *s)
{
    if (s->headers_str || !s->response)
        return;

    size_t cap = 256;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return;
    size_t pos = 0;

    for (int i = 0; i < s->response->header_count; i++)
    {
        const char *name = s->response->headers[i].name;
        const char *value = s->response->headers[i].value;
        if (!name || !value)
            continue;
        size_t nl = strlen(name);
        size_t vl = strlen(value);
        size_t need = nl + 2 + vl + 2; 
        if (pos + need + 1 > cap)
        {
            while (pos + need + 1 > cap)
                cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb)
            {
                free(buf);
                return;
            }
            buf = nb;
        }
        memcpy(buf + pos, name, nl);
        pos += nl;
        buf[pos++] = ':';
        buf[pos++] = ' ';
        memcpy(buf + pos, value, vl);
        pos += vl;
        buf[pos++] = '\r';
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    s->headers_str = buf;
    s->headers_str_len = pos;
}

static int parse_headers_blob(const char *blob, size_t len,
                              NetworkHeader *out, int max)
{
    int count = 0;
    size_t i = 0;
    while (i < len && count < max)
    {
        
        while (i < len && (blob[i] == '\r' || blob[i] == '\n'))
            i++;
        if (i >= len)
            break;

        size_t name_start = i;
        while (i < len && blob[i] != ':' && blob[i] != '\n' && blob[i] != '\r')
            i++;
        if (i >= len || blob[i] != ':')
            break;
        size_t name_end = i;
        i++; 
        while (i < len && (blob[i] == ' ' || blob[i] == '\t'))
            i++;

        size_t val_start = i;
        while (i < len && blob[i] != '\r' && blob[i] != '\n')
            i++;
        size_t val_end = i;

        size_t nl = name_end - name_start;
        size_t vl = val_end - val_start;
        if (nl == 0)
            continue;

        out[count].name = (char *)malloc(nl + 1);
        out[count].value = (char *)malloc(vl + 1);
        if (!out[count].name || !out[count].value)
        {
            free(out[count].name);
            free(out[count].value);
            break;
        }
        memcpy(out[count].name, blob + name_start, nl);
        out[count].name[nl] = '\0';
        memcpy(out[count].value, blob + val_start, vl);
        out[count].value[vl] = '\0';
        count++;
    }
    return count;
}

static void free_parsed_headers(NetworkHeader *h, int count)
{
    for (int i = 0; i < count; i++)
    {
        free(h[i].name);
        free(h[i].value);
    }
}

static void wasm_fetch_async_callback(int request_id, NetworkResponse *response,
                                      const char *error, void *user_data)
{
    WasmFetchCompletion *completion = (WasmFetchCompletion *)user_data;
    WasmNetworkBindingState *state = completion ? completion->state : NULL;
    if (!completion || !completion->active || !state ||
        state->shutting_down ||
        state->generation != completion->state_generation ||
        completion->slot_id < 0 ||
        completion->slot_id >= WASM_NET_MAX_PENDING)
    {
        network_response_free(response);
        return;
    }

    WasmFetchSlot *s = &state->slots[completion->slot_id];
    uint32_t token = budo_wasm_network_token(
        completion->slot_generation, (uint32_t)completion->slot_id);
    if (!budo_wasm_network_completion_matches(s->in_use, s->ready,
                                              s->generation, s->async_id,
                                              token, request_id) ||
        s->completion != completion ||
        completion->async_id != request_id)
    {
        network_response_free(response);
        completion->active = false;
        completion->state = NULL;
        return;
    }

    s->response = response;
    if (error && !response)
    {
        s->error = strdup(error);
    }
    s->ready = true;
    s->completion = NULL;
    completion->active = false;
    completion->state = NULL;
}

static bool wasm_get_memory_view(WasmNetworkBindingState *state,
                                 uint8_t **mem_out, size_t *size_out)
{
    if (!state || state->shutting_down || !state->store_ctx ||
        !state->has_memory)
        return false;
    *mem_out = wasmtime_memory_data(state->store_ctx, &state->memory);
    *size_out = wasmtime_memory_data_size(state->store_ctx, &state->memory);
    return *mem_out != NULL;
}

static int32_t copy_to_wasm(WasmNetworkBindingState *state,
                            int32_t dst_ptr, int32_t dst_max,
                            const void *src, int32_t src_len)
{
    uint8_t *mem = NULL;
    size_t mem_size = 0;
    if (!wasm_get_memory_view(state, &mem, &mem_size))
        return 0;
    if (dst_max <= 0 || !budo_wasm_memory_range_valid(dst_ptr, dst_max, mem_size))
        return 0;
    int32_t n = src_len < dst_max ? src_len : dst_max;
    if (n > 0)
        memcpy(mem + dst_ptr, src, (size_t)n);
    return n;
}

static wasm_trap_t *host_network_fetch(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults >= 1)
    {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = -1;
    }
    if (nargs < 8 || !state || state->shutting_down || !state->network_ctx)
        return NULL;
    if (!network_is_enabled(state->network_ctx))
        return NULL;

    uint8_t *mem = NULL;
    size_t mem_size = 0;
    if (!wasm_get_memory_view(state, &mem, &mem_size))
        return NULL;

    int32_t method_ptr = args[0].of.i32;
    int32_t method_len = args[1].of.i32;
    int32_t url_ptr = args[2].of.i32;
    int32_t url_len = args[3].of.i32;
    int32_t headers_ptr = args[4].of.i32;
    int32_t headers_len = args[5].of.i32;
    int32_t body_ptr = args[6].of.i32;
    int32_t body_len = args[7].of.i32;

    if (method_len < 0 || method_len > 16)
        return NULL;
    if (url_len <= 0 || url_len > 8192)
        return NULL;
    if (headers_len < 0 || headers_len > 65536)
        return NULL;
    if (body_len < 0 || body_len > (1 << 24))
        return NULL;
    if (!budo_wasm_memory_range_valid(method_ptr, method_len, mem_size) ||
        !budo_wasm_memory_range_valid(url_ptr, url_len, mem_size) ||
        !budo_wasm_memory_range_valid(headers_ptr, headers_len, mem_size) ||
        !budo_wasm_memory_range_valid(body_ptr, body_len, mem_size))
        return NULL;

    char method[17] = "GET";
    if (method_len > 0)
    {
        memcpy(method, mem + method_ptr, method_len);
        method[method_len] = '\0';
    }

    char url[8193];
    memcpy(url, mem + url_ptr, url_len);
    url[url_len] = '\0';

    NetworkHeader req_headers[64];
    int req_header_count = 0;
    if (headers_len > 0)
    {
        req_header_count = parse_headers_blob((const char *)(mem + headers_ptr),
                                              (size_t)headers_len,
                                              req_headers, 64);
    }

    int slot_id = alloc_slot(state);
    if (slot_id < 0)
    {
        free_parsed_headers(req_headers, req_header_count);
        return NULL;
    }

    WasmFetchCompletion *completion = alloc_completion(state);
    if (!completion)
    {
        free_slot(state, slot_id);
        free_parsed_headers(req_headers, req_header_count);
        return NULL;
    }

    WasmFetchSlot *slot = &state->slots[slot_id];
    completion->state = state;
    completion->slot_id = slot_id;
    completion->state_generation = state->generation;
    completion->slot_generation = slot->generation;
    slot->completion = completion;

    ApiError api_error;
    int async_id = network_service_request_async(
        state->network_ctx, method, url,
        req_header_count > 0 ? req_headers : NULL, req_header_count,
        body_len > 0 ? mem + body_ptr : NULL, (size_t)body_len,
        wasm_fetch_async_callback, completion, &api_error);

    free_parsed_headers(req_headers, req_header_count);

    if (async_id < 0)
    {
        slot->ready = true;
        slot->completion = NULL;
        completion->active = false;
        completion->state = NULL;
        const char *err = network_get_error(state->network_ctx);
        slot->error = strdup(err ? err : "fetch failed");
    }
    else
    {
        slot->async_id = async_id;
        completion->async_id = async_id;
    }

    if (nresults >= 1)
        results[0].of.i32 = slot_id;
    return NULL;
}

static wasm_trap_t *host_network_fetch_status(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (nargs < 1)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s)
        return NULL;
    results[0].of.i32 = s->ready ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_network_fetch_status_code(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (nargs < 1)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response)
        return NULL;
    results[0].of.i32 = s->response->status;
    return NULL;
}

static wasm_trap_t *host_network_fetch_response_body_len(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (nargs < 1)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response)
        return NULL;
    results[0].of.i32 = (int32_t)s->response->body_len;
    return NULL;
}

static wasm_trap_t *host_network_fetch_response_body(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;
    if (nargs < 3)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response || !s->response->body)
        return NULL;
    results[0].of.i32 = copy_to_wasm(state, args[1].of.i32, args[2].of.i32,
                                     s->response->body,
                                     (int32_t)s->response->body_len);
    return NULL;
}

static wasm_trap_t *host_network_fetch_response_headers_len(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (nargs < 1)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response)
        return NULL;
    ensure_headers_str(s);
    results[0].of.i32 = (int32_t)s->headers_str_len;
    return NULL;
}

static wasm_trap_t *host_network_fetch_response_headers(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;
    if (nargs < 3)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response)
        return NULL;
    ensure_headers_str(s);
    if (!s->headers_str)
        return NULL;
    results[0].of.i32 = copy_to_wasm(state, args[1].of.i32, args[2].of.i32,
                                     s->headers_str,
                                     (int32_t)s->headers_str_len);
    return NULL;
}

static wasm_trap_t *host_network_fetch_response_url_len(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (nargs < 1)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response || !s->response->url)
        return NULL;
    results[0].of.i32 = (int32_t)strlen(s->response->url);
    return NULL;
}

static wasm_trap_t *host_network_fetch_response_url(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;
    if (nargs < 3)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->response || !s->response->url)
        return NULL;
    results[0].of.i32 = copy_to_wasm(state, args[1].of.i32, args[2].of.i32,
                                     s->response->url,
                                     (int32_t)strlen(s->response->url));
    return NULL;
}

static wasm_trap_t *host_network_fetch_error_len(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;
    if (nargs < 1)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->error)
        return NULL;
    results[0].of.i32 = (int32_t)strlen(s->error);
    return NULL;
}

static wasm_trap_t *host_network_fetch_error(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    if (nresults < 1)
        return NULL;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;
    if (nargs < 3)
        return NULL;
    WasmFetchSlot *s = get_slot(state, args[0].of.i32);
    if (!s || !s->ready || !s->error)
        return NULL;
    results[0].of.i32 = copy_to_wasm(state, args[1].of.i32, args[2].of.i32,
                                     s->error, (int32_t)strlen(s->error));
    return NULL;
}

static wasm_trap_t *host_network_fetch_release(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNetworkBindingState *state = (WasmNetworkBindingState *)env;
    (void)caller;
    (void)results;
    (void)nresults;
    if (nargs < 1)
        return NULL;
    free_slot(state, args[0].of.i32);
    return NULL;
}

static wasmtime_error_t *define_network_func(
    wasmtime_linker_t *linker, WasmNetworkBindingState *state,
    const char *name, wasmtime_func_callback_t callback,
    const wasm_valkind_t *param_types, size_t param_count,
    const wasm_valkind_t *result_types, size_t result_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t index = 0; index < param_count; index++)
        params.data[index] = wasm_valtype_new(param_types[index]);
    wasm_valtype_vec_new_uninitialized(&results, result_count);
    for (size_t index = 0; index < result_count; index++)
        results.data[index] = wasm_valtype_new(result_types[index]);

    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name), functype,
        callback, state, NULL);
    wasm_functype_delete(functype);
    return error;
}

wasmtime_error_t *wasm_network_register_state(
    wasmtime_linker_t *linker, WasmNetworkBindingState *state)
{
    wasmtime_error_t *error = NULL;

    if (!linker || !state || !state->network_ctx || state->shutting_down)
        return wasmtime_error_new("WASM network binding state is required");

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32,
                                   WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_network_func(linker, state, "network_fetch",
                                    host_network_fetch, params, 8, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_network_func(linker, state, "network_fetch_status",
                                    host_network_fetch_status, params, 1, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_status_code",
                                    host_network_fetch_status_code, params, 1, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_response_body_len",
                                    host_network_fetch_response_body_len, params, 1, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_response_headers_len",
                                    host_network_fetch_response_headers_len, params, 1, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_response_url_len",
                                    host_network_fetch_response_url_len, params, 1, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_error_len",
                                    host_network_fetch_error_len, params, 1, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_network_func(linker, state, "network_fetch_response_body",
                                    host_network_fetch_response_body, params, 3, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_response_headers",
                                    host_network_fetch_response_headers, params, 3, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_response_url",
                                    host_network_fetch_response_url, params, 3, results, 1);
        if (error)
            return error;
        error = define_network_func(linker, state, "network_fetch_error",
                                    host_network_fetch_error, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_network_func(linker, state, "network_fetch_release",
                                    host_network_fetch_release, params, 1, NULL, 0);
        if (error)
            return error;
    }

    return NULL;
}

wasmtime_error_t *wasm_network_register(wasmtime_linker_t *linker)
{
    return wasm_network_register_state(
        linker, (WasmNetworkBindingState *)wasm_binding_scope_get(
                    &wasm_network_scope_owner));
}

WasmNetworkBindingState *wasm_network_binding_state_create(
    const char *project_dir)
{
    WasmNetworkBindingState *state =
        (WasmNetworkBindingState *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;

    NetworkPolicy policy;
    network_policy_load_app_json(&policy, project_dir);

    state->network_ctx = network_create(&policy);
    if (!state->network_ctx)
    {
        fprintf(stderr, "wasm_network: failed to create context\n");
        free(state);
        return NULL;
    }
    state->generation = 1;
    state->next_slot_generation = 1;

    if (network_is_enabled(state->network_ctx))
    {
        if (policy.allow_all)
            printf("Network: enabled (all domains)\n");
        else
            printf("Network: enabled (%d domain(s))\n", policy.domain_count);
    }
    return state;
}

NetworkContext *wasm_network_context(WasmNetworkBindingState *state)
{
    return state ? state->network_ctx : NULL;
}

void wasm_network_binding_scope_enter(WasmNetworkBindingState *state)
{
    wasm_binding_scope_enter(&wasm_network_scope_owner, state);
}

void wasm_network_binding_scope_leave(WasmNetworkBindingState *state)
{
    wasm_binding_scope_leave(&wasm_network_scope_owner, state);
}

void wasm_network_binding_state_set_memory(
    WasmNetworkBindingState *state, wasmtime_context_t *store_ctx,
    wasmtime_memory_t *memory)
{
    if (!state || state->shutting_down || !store_ctx || !memory)
        return;
    state->store_ctx = store_ctx;
    state->memory = *memory;
    state->has_memory = true;
}

void wasm_network_set_memory(wasmtime_context_t *store_ctx,
                             wasmtime_memory_t *memory)
{
    wasm_network_binding_state_set_memory(
        (WasmNetworkBindingState *)wasm_binding_scope_get(
            &wasm_network_scope_owner),
        store_ctx, memory);
}

void wasm_network_poll(WasmNetworkBindingState *state)
{
    if (state && !state->shutting_down && state->network_ctx)
        network_async_poll(state->network_ctx);
}

void wasm_network_shutdown(WasmNetworkBindingState *state)
{
    if (!state || state->shutting_down)
        return;

    state->shutting_down = true;
    state->generation++;
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        state->completions[i].active = false;
        state->completions[i].state = NULL;
    }

    network_shutdown(state->network_ctx);
    state->has_memory = false;
    state->store_ctx = NULL;
    memset(&state->memory, 0, sizeof(state->memory));
}

void wasm_network_cleanup(WasmNetworkBindingState *state)
{
    if (!state)
        return;

    wasm_network_shutdown(state);
    for (int i = 0; i < WASM_NET_MAX_PENDING; i++)
    {
        if (state->slots[i].in_use)
            free_slot(state, i);
    }
    network_destroy(state->network_ctx);
    state->network_ctx = NULL;
    free(state);
}

void wasm_network_binding_state_destroy(WasmNetworkBindingState *state)
{
    wasm_network_cleanup(state);
}