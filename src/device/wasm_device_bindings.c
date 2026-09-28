#include "wasm_device_bindings.h"
#include "device_service.h"

#include <stdio.h>
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

wasmtime_error_t *wasm_device_register(wasmtime_linker_t *linker,
                                       DeviceContext *state)
{
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