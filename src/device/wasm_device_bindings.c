#include "wasm_device_bindings.h"
#include "device_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wasm_trap_t *host_device_keep_screen_on(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    ApiError error;
    DeviceContext *state = (DeviceContext *)env;
    (void)caller;
    (void)nargs;
    (void)nresults;
    bool enabled = (nargs >= 1 && args[0].of.i32 != 0);
    bool ok = device_binding_state_keep_screen_on(state, enabled, &error);
    if (!ok)
        fprintf(stderr, "%s: %s\n", error.code, error.message);
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ok ? 1 : 0;
    return NULL;
}

static uint8_t *guest_bytes(wasmtime_caller_t *caller, int32_t ptr, int32_t len)
{
    wasmtime_extern_t item;
    if (!caller || ptr < 0 || len < 0 ||
        !wasmtime_caller_export_get(caller, "memory", 6, &item) || item.kind != WASMTIME_EXTERN_MEMORY)
        return NULL;
    wasmtime_context_t *context = wasmtime_caller_context(caller);
    size_t size = wasmtime_memory_data_size(context, &item.of.memory);
    if ((size_t)ptr + (size_t)len > size)
        return NULL;
    return wasmtime_memory_data(context, &item.of.memory) + ptr;
}

static void set_i32(wasmtime_val_t *results, int32_t value)
{
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = value;
}

static wasm_trap_t *host_device_set_clipboard_text(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                                   size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)nargs, (void)nresults;
    const uint8_t *bytes = guest_bytes(caller, args[0].of.i32, args[1].of.i32);
    char *text = bytes ? malloc((size_t)args[1].of.i32 + 1) : NULL;
    bool ok = false;
    if (text)
    {
        memcpy(text, bytes, (size_t)args[1].of.i32);
        text[args[1].of.i32] = '\0';
        ok = device_set_clipboard_text(text);
        free(text);
    }
    set_i32(results, ok ? 1 : 0);
    return NULL;
}

static wasm_trap_t *host_device_get_clipboard_text(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                                   size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)nargs, (void)nresults;
    char *text = device_get_clipboard_text();
    int32_t length = text ? (int32_t)strlen(text) : -1;
    if (text && args[1].of.i32 > 0)
    {
        int32_t count = length < args[1].of.i32 ? length : args[1].of.i32;
        uint8_t *out = guest_bytes(caller, args[0].of.i32, count);
        if (out)
            memcpy(out, text, (size_t)count);
    }
    free(text);
    set_i32(results, length);
    return NULL;
}

static wasm_trap_t *host_device_haptic(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                       size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)caller, (void)nargs, (void)nresults;
    int32_t kind = args[0].of.i32;
    set_i32(results, kind >= 0 && kind < DEVICE_HAPTIC_COUNT && device_haptic((DeviceHaptic)kind) ? 1 : 0);
    return NULL;
}

static wasm_trap_t *host_device_get_preference(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                               size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)caller, (void)nargs, (void)nresults;
    DevicePreferences p;
    device_get_preferences(&p);
    const float values[] = {p.dark_mode, p.reduced_motion, p.high_contrast, p.font_scale,
                            p.safe_top, p.safe_right, p.safe_bottom, p.safe_left, p.keyboard_inset};
    int32_t id = args[0].of.i32;
    results[0].kind = WASMTIME_F32;
    results[0].of.f32 = id >= 0 && id < (int32_t)(sizeof(values) / sizeof(values[0])) ? values[id] : 0.0f;
    return NULL;
}

static wasm_trap_t *host_device_set_cursor(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
                                           size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env, (void)caller, (void)nargs, (void)nresults;
    int32_t cursor = args[0].of.i32;
    set_i32(results, cursor >= 0 && cursor < DEVICE_CURSOR_COUNT && device_set_cursor((DeviceCursor)cursor) ? 1 : 0);
    return NULL;
}

static wasmtime_error_t *define_device_function(wasmtime_linker_t *linker, DeviceContext *state, const char *name,
                                                wasmtime_func_callback_t callback, const wasm_valkind_t *params,
                                                size_t param_count, wasm_valkind_t result)
{
    wasm_valtype_vec_t param_types, result_types;
    wasm_valtype_vec_new_uninitialized(&param_types, param_count);
    for (size_t i = 0; i < param_count; i++)
        param_types.data[i] = wasm_valtype_new(params[i]);
    wasm_valtype_vec_new_uninitialized(&result_types, 1);
    result_types.data[0] = wasm_valtype_new(result);
    wasm_functype_t *type = wasm_functype_new(&param_types, &result_types);
    wasmtime_error_t *error = wasmtime_linker_define_func(linker, "env", 3, name, strlen(name), type,
                                                          callback, state, NULL);
    wasm_functype_delete(type);
    return error;
}

wasmtime_error_t *wasm_device_register(wasmtime_linker_t *linker,
                                       DeviceContext *state)
{
    static const wasm_valkind_t pair[] = {WASM_I32, WASM_I32};
    static const wasm_valkind_t one[] = {WASM_I32};
    wasmtime_error_t *extra = NULL;
    if (linker && state &&
        ((extra = define_device_function(linker, state, "device_set_clipboard_text", host_device_set_clipboard_text, pair, 2, WASM_I32)) ||
         (extra = define_device_function(linker, state, "device_get_clipboard_text", host_device_get_clipboard_text, pair, 2, WASM_I32)) ||
         (extra = define_device_function(linker, state, "device_haptic", host_device_haptic, one, 1, WASM_I32)) ||
         (extra = define_device_function(linker, state, "device_get_preference", host_device_get_preference, one, 1, WASM_F32)) ||
         (extra = define_device_function(linker, state, "device_set_cursor", host_device_set_cursor, one, 1, WASM_I32))))
        return extra;

    if (!linker || !state)
        return wasmtime_error_new("WASM device binding state is required");
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, 1);
    params.data[0] = wasm_valtype_new(WASM_I32);
    wasm_valtype_vec_new_uninitialized(&results, 1);
    results.data[0] = wasm_valtype_new(WASM_I32);

    wasm_functype_t *func_type = wasm_functype_new(&params, &results);
    const char name[] = "device_keep_screen_on";
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name),
        func_type, host_device_keep_screen_on, state, NULL);
    wasm_functype_delete(func_type);
    return error;
}