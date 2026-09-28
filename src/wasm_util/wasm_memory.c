#include "wasm_util/wasm_memory.h"

#include <string.h>

#ifdef _MSC_VER
#define BUDO_WASM_THREAD_LOCAL __declspec(thread)
#else
#define BUDO_WASM_THREAD_LOCAL _Thread_local
#endif

typedef struct WasmBindingScope
{
    const void *owner;
    void *state;
} WasmBindingScope;

static BUDO_WASM_THREAD_LOCAL WasmBindingScope active_binding_scope;

void wasm_binding_scope_enter(const void *owner, void *state)
{
    if (!owner || !state || active_binding_scope.owner)
        return;
    active_binding_scope.owner = owner;
    active_binding_scope.state = state;
}

void *wasm_binding_scope_get(const void *owner)
{
    return owner && active_binding_scope.owner == owner
               ? active_binding_scope.state
               : NULL;
}

void wasm_binding_scope_leave(const void *owner, void *state)
{
    if (active_binding_scope.owner != owner ||
        active_binding_scope.state != state)
        return;
    memset(&active_binding_scope, 0, sizeof(active_binding_scope));
}

bool wasm_mem_read_str(wasmtime_context_t *store, wasmtime_memory_t *mem,
                       int32_t ptr, int32_t len,
                       char *buffer, size_t buffer_size)
{
    if (!store || !mem || !buffer || buffer_size == 0)
        return false;
    if (ptr < 0 || len < 0)
        return false;
    if ((size_t)len + 1 > buffer_size)
        return false;

    uint8_t *base = wasmtime_memory_data(store, mem);
    size_t mem_size = wasmtime_memory_data_size(store, mem);
    size_t end = (size_t)ptr + (size_t)len;

    if (end < (size_t)ptr || end > mem_size)
        return false;

    memcpy(buffer, base + ptr, (size_t)len);
    buffer[len] = '\0';
    return true;
}

int32_t wasm_mem_write_str(wasmtime_context_t *store, wasmtime_memory_t *mem,
                           int32_t ptr, int32_t max_len,
                           const char *str, int32_t str_len)
{
    if (!store || !mem || !str)
        return 0;
    if (ptr < 0 || max_len <= 0)
        return 0;

    uint8_t *base = wasmtime_memory_data(store, mem);
    size_t mem_size = wasmtime_memory_data_size(store, mem);

    int32_t write_len = str_len < max_len - 1 ? str_len : max_len - 1;
    if (write_len < 0)
        write_len = 0;
    size_t end = (size_t)ptr + (size_t)write_len + 1; 
    if (end > mem_size)
        return 0;

    memcpy(base + ptr, str, (size_t)write_len);
    base[ptr + write_len] = '\0';
    return write_len;
}

int32_t wasm_mem_write_bytes(wasmtime_context_t *store, wasmtime_memory_t *mem,
                             int32_t ptr, int32_t max_len,
                             const uint8_t *data, int32_t data_len)
{
    if (!store || !mem || !data)
        return 0;
    if (ptr < 0 || max_len <= 0)
        return 0;

    uint8_t *base = wasmtime_memory_data(store, mem);
    size_t mem_size = wasmtime_memory_data_size(store, mem);

    int32_t write_len = data_len < max_len ? data_len : max_len;
    if (write_len < 0)
        write_len = 0;
    size_t end = (size_t)ptr + (size_t)write_len;
    if (end > mem_size)
        return 0;

    memcpy(base + ptr, data, (size_t)write_len);
    return write_len;
}

wasmtime_error_t *wasm_define_host_func(
    wasmtime_linker_t *linker,
    const char *name,
    wasmtime_func_callback_t callback,
    const wasm_valkind_t *param_kinds, size_t param_count,
    const wasm_valkind_t *result_kinds, size_t result_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t i = 0; i < param_count; i++)
        params.data[i] = wasm_valtype_new(param_kinds[i]);
    wasm_valtype_vec_new_uninitialized(&results, result_count);
    for (size_t i = 0; i < result_count; i++)
        results.data[i] = wasm_valtype_new(result_kinds[i]);

    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name),
        functype, callback, NULL, NULL);
    wasm_functype_delete(functype);
    return error;
}