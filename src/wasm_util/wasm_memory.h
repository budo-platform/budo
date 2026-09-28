#ifndef BUDO_WASM_MEMORY_H
#define BUDO_WASM_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wasmtime.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void wasm_binding_scope_enter(const void *owner, void *state);
    void *wasm_binding_scope_get(const void *owner);
    void wasm_binding_scope_leave(const void *owner, void *state);

    bool wasm_mem_read_str(wasmtime_context_t *store, wasmtime_memory_t *mem,
                           int32_t ptr, int32_t len,
                           char *buffer, size_t buffer_size);

    int32_t wasm_mem_write_str(wasmtime_context_t *store, wasmtime_memory_t *mem,
                               int32_t ptr, int32_t max_len,
                               const char *str, int32_t str_len);

    int32_t wasm_mem_write_bytes(wasmtime_context_t *store, wasmtime_memory_t *mem,
                                 int32_t ptr, int32_t max_len,
                                 const uint8_t *data, int32_t data_len);

    wasmtime_error_t *wasm_define_host_func(
        wasmtime_linker_t *linker,
        const char *name,
        wasmtime_func_callback_t callback,
        const wasm_valkind_t *param_kinds, size_t param_count,
        const wasm_valkind_t *result_kinds, size_t result_count);

#ifdef __cplusplus
}
#endif

#endif