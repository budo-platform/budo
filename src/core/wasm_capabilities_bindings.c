#include "wasm_capabilities_bindings.h"
#include "capabilities.h"

#include <string.h>

#define DEFINE_CAP_HOST(NAME, FIELD)                                       \
    static wasm_trap_t *host_capability_##NAME(                            \
        void *env, wasmtime_caller_t *caller,                              \
        const wasmtime_val_t *args, size_t nargs,                          \
        wasmtime_val_t *results, size_t nresults)                          \
    {                                                                      \
        ApiError error;                                                    \
        CapabilitySnapshot snapshot;                                       \
        (void)env;                                                         \
        (void)caller;                                                      \
        (void)args;                                                        \
        (void)nargs;                                                       \
        (void)nresults;                                                    \
        results[0].kind = WASMTIME_I32;                                    \
        results[0].of.i32 =                                                \
            capabilities_get_snapshot(&snapshot, &error) && snapshot.FIELD \
                ? 1                                                        \
                : 0;                                                       \
        return NULL;                                                       \
    }

DEFINE_CAP_HOST(neural, neural)
DEFINE_CAP_HOST(midi, midi)
DEFINE_CAP_HOST(udp, udp)
DEFINE_CAP_HOST(http, http)
DEFINE_CAP_HOST(sensors, sensors)

static wasm_trap_t *host_capability_llamacpp(
    void *env, wasmtime_caller_t *caller, const wasmtime_val_t *args,
    size_t nargs, wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;
    return NULL;
}

static wasmtime_error_t *define_cap(wasmtime_linker_t *linker,
                                    const char *name,
                                    wasmtime_func_callback_t cb)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_empty(&params);
    wasm_valtype_vec_new_uninitialized(&results, 1);
    results.data[0] = wasm_valtype_new(WASM_I32);

    wasm_functype_t *func_type = wasm_functype_new(&params, &results);
    wasmtime_error_t *err = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name),
        func_type, cb, NULL, NULL);
    wasm_functype_delete(func_type);
    return err;
}

wasmtime_error_t *wasm_capabilities_register(wasmtime_linker_t *linker)
{
    wasmtime_error_t *err = NULL;
    if ((err = define_cap(linker, "capability_neural", host_capability_neural)))
        return err;
    if ((err = define_cap(linker, "capability_llamacpp", host_capability_llamacpp)))
        return err;
    if ((err = define_cap(linker, "capability_midi", host_capability_midi)))
        return err;
    if ((err = define_cap(linker, "capability_udp", host_capability_udp)))
        return err;
    if ((err = define_cap(linker, "capability_http", host_capability_http)))
        return err;
    if ((err = define_cap(linker, "capability_sensors", host_capability_sensors)))
        return err;
    return NULL;
}