#include "accessibility/wasm_accessibility_bindings.h"
#include "accessibility/accessibility_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *guest_bytes(wasmtime_caller_t *caller, int32_t ptr, int32_t len)
{
    wasmtime_extern_t item;
    if (!caller || ptr < 0 || len < 0 ||
        !wasmtime_caller_export_get(caller, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY)
        return NULL;
    wasmtime_context_t *context = wasmtime_caller_context(caller);
    if ((size_t)ptr + (size_t)len > wasmtime_memory_data_size(context, &item.of.memory))
        return NULL;
    return wasmtime_memory_data(context, &item.of.memory) + ptr;
}

static void set_i32(wasmtime_val_t *results, int32_t value)
{
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = value;
}

static wasm_trap_t *host_is_available(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                      size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)caller, (void)args, (void)nargs, (void)nresults;
    set_i32(results, accessibility_available() ? 1 : 0);
    return NULL;
}

static wasm_trap_t *host_is_active(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                   size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)caller, (void)args, (void)nargs, (void)nresults;
    set_i32(results, accessibility_active() ? 1 : 0);
    return NULL;
}

static wasm_trap_t *host_update(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)nargs, (void)nresults;
    const uint8_t *bytes = guest_bytes(caller, args[0].of.i32, args[1].of.i32);
    char *json = bytes ? malloc((size_t)args[1].of.i32 + 1) : NULL;
    AccessibilityNode *nodes = json ? calloc(ACCESSIBILITY_MAX_NODES, sizeof(AccessibilityNode)) : NULL;
    int ok = 0;
    if (nodes)
    {
        memcpy(json, bytes, (size_t)args[1].of.i32);
        json[args[1].of.i32] = '\0';
        int count = accessibility_parse_json(json, nodes, ACCESSIBILITY_MAX_NODES);
        ok = count >= 0 && accessibility_update(nodes, count);
    }
    free(nodes);
    free(json);
    set_i32(results, ok);
    return NULL;
}

static size_t append_json_string(char *out, size_t at, size_t size, const char *text)
{
    if (at < size)
        out[at] = '"';
    at++;
    for (const char *p = text; *p; p++)
    {
        char escaped[8];
        unsigned char c = (unsigned char)*p;
        int length = c == '"' || c == '\\' ? snprintf(escaped, sizeof(escaped), "\\%c", c)
                     : c < 0x20            ? snprintf(escaped, sizeof(escaped), "\\u%04x", c)
                                           : (escaped[0] = (char)c, 1);
        for (int i = 0; i < length; i++, at++)
            if (at < size)
                out[at] = escaped[i];
    }
    if (at < size)
        out[at] = '"';
    return at + 1;
}

static wasm_trap_t *host_take_actions(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                      size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)nargs, (void)nresults;
    AccessibilityAction actions[ACCESSIBILITY_MAX_ACTIONS];
    
    size_t size = ACCESSIBILITY_MAX_ACTIONS * (sizeof(AccessibilityAction) * 6 + 64) + 2;
    char *json = malloc(size);
    if (!json)
    {
        set_i32(results, -1);
        return NULL;
    }
    int count = accessibility_take_actions(actions, ACCESSIBILITY_MAX_ACTIONS);
    size_t at = 0;
    json[at++] = '[';
    for (int i = 0; i < count; i++)
    {
        at += (size_t)snprintf(json + at, size - at, "%s{\"id\":", i ? "," : "");
        at = append_json_string(json, at, size, actions[i].id);
        at += (size_t)snprintf(json + at, size - at, ",\"action\":");
        at = append_json_string(json, at, size, actions[i].action);
        at += (size_t)snprintf(json + at, size - at, ",\"value\":");
        at = append_json_string(json, at, size, actions[i].value);
        json[at++] = '}';
    }
    json[at++] = ']';
    int32_t max = args[1].of.i32;
    uint8_t *out = (int32_t)at <= max ? guest_bytes(caller, args[0].of.i32, (int32_t)at) : NULL;
    if (out)
        memcpy(out, json, at);
    else
        for (int i = 0; i < count; i++)
            accessibility_post_action(actions[i].id, actions[i].action, actions[i].value);
    free(json);
    set_i32(results, (int32_t)at);
    return NULL;
}

static wasmtime_error_t *define_accessibility_function(wasmtime_linker_t *linker, const char *name,
                                                       wasmtime_func_callback_t callback, size_t param_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t i = 0; i < param_count; i++)
        params.data[i] = wasm_valtype_new(WASM_I32);
    wasm_valtype_vec_new_uninitialized(&results, 1);
    results.data[0] = wasm_valtype_new(WASM_I32);
    wasm_functype_t *type = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(linker, "env", 3, name, strlen(name), type, callback,
                                                          NULL, NULL);
    wasm_functype_delete(type);
    return error;
}

wasmtime_error_t *wasm_accessibility_register(wasmtime_linker_t *linker)
{
    wasmtime_error_t *error;
    if ((error = define_accessibility_function(linker, "accessibility_is_available", host_is_available, 0)) ||
        (error = define_accessibility_function(linker, "accessibility_is_active", host_is_active, 0)) ||
        (error = define_accessibility_function(linker, "accessibility_update", host_update, 2)) ||
        (error = define_accessibility_function(linker, "accessibility_take_actions", host_take_actions, 2)))
        return error;
    return NULL;
}