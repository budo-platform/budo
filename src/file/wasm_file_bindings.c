#include "wasm_util/wasm_memory.h"
#include "wasm_file_bindings.h"
#include "file_service.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct WasmFileBindingState
{
    FileContext *file_ctx;
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool has_memory;
};

static const char wasm_file_scope_owner;

static bool read_wasm_str(WasmFileBindingState *state, int32_t ptr,
                          int32_t len, char *buffer, size_t buffer_size)
{
    if (!state || !state->store_ctx || !state->has_memory)
        return false;
    return wasm_mem_read_str(state->store_ctx, &state->memory,
                             ptr, len, buffer, buffer_size);
}

static int32_t write_wasm_str(WasmFileBindingState *state, int32_t ptr,
                              int32_t max_len, const char *str, int32_t str_len)
{
    if (!state || !state->store_ctx || !state->has_memory)
        return 0;
    return wasm_mem_write_str(state->store_ctx, &state->memory,
                              ptr, max_len, str, str_len);
}

static int32_t write_wasm_bytes(WasmFileBindingState *state, int32_t ptr,
                                int32_t max_len, const uint8_t *data,
                                int32_t data_len)
{
    if (!state || !state->store_ctx || !state->has_memory)
        return 0;
    return wasm_mem_write_bytes(state->store_ctx, &state->memory,
                                ptr, max_len, data, data_len);
}

static wasmtime_error_t *define_file_func(
    wasmtime_linker_t *linker,
    WasmFileBindingState *state,
    const char *name,
    wasmtime_func_callback_t callback,
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

static wasm_trap_t *host_file_list_json(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmFileBindingState *state = (WasmFileBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->file_ctx || nargs < 4)
        return NULL;

    if (!file_has_root(state->file_ctx))
    {
        results[0].of.i32 = -1;
        return NULL;
    }

    char path[FILE_MAX_PATH];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32,
                       path, sizeof(path)))
        return NULL;

    FileListResult *listing = file_list(state->file_ctx, path);
    if (!listing)
    {
        results[0].of.i32 = -1;
        return NULL;
    }

    size_t json_cap = 4096;
    char *json = (char *)malloc(json_cap);
    if (!json)
    {
        file_list_free(listing);
        return NULL;
    }

    size_t pos = 0;
    json[pos++] = '[';

    for (int i = 0; i < listing->count; i++)
    {
        if (i > 0)
            json[pos++] = ',';

        if (pos + 512 > json_cap)
        {
            json_cap *= 2;
            char *new_json = (char *)realloc(json, json_cap);
            if (!new_json)
            {
                free(json);
                file_list_free(listing);
                return NULL;
            }
            json = new_json;
        }

        const char *type_str = listing->entries[i].type == FILE_ENTRY_DIRECTORY ? "directory" : "file";
        if (listing->entries[i].type == FILE_ENTRY_FILE && listing->entries[i].size >= 0)
        {
            pos += snprintf(json + pos, json_cap - pos,
                            "{\"name\":\"%s\",\"type\":\"%s\",\"size\":%lld}",
                            listing->entries[i].name, type_str,
                            (long long)listing->entries[i].size);
        }
        else
        {
            pos += snprintf(json + pos, json_cap - pos,
                            "{\"name\":\"%s\",\"type\":\"%s\"}",
                            listing->entries[i].name, type_str);
        }
    }
    json[pos++] = ']';
    json[pos] = '\0';

    file_list_free(listing);

    results[0].of.i32 = write_wasm_str(state, args[2].of.i32,
                                       args[3].of.i32, json, (int32_t)pos);
    free(json);
    return NULL;
}

static wasm_trap_t *host_file_read_text(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmFileBindingState *state = (WasmFileBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->file_ctx || nargs < 4)
        return NULL;

    if (!file_has_root(state->file_ctx))
        return NULL;

    char path[FILE_MAX_PATH];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32,
                       path, sizeof(path)))
        return NULL;

    size_t len = 0;
    ApiError error;
    char *content = file_service_read_text_virtual(state->file_ctx, path, &len,
                                                   &error);
    if (!content)
        return NULL;

    results[0].of.i32 = write_wasm_str(state, args[2].of.i32,
                                       args[3].of.i32, content, (int32_t)len);
    free(content);
    return NULL;
}

static wasm_trap_t *host_file_read_binary(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmFileBindingState *state = (WasmFileBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->file_ctx || nargs < 4)
        return NULL;

    if (!file_has_root(state->file_ctx))
        return NULL;

    char path[FILE_MAX_PATH];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32,
                       path, sizeof(path)))
        return NULL;

    size_t len = 0;
    uint8_t *data = file_read_binary(state->file_ctx, path, &len);
    if (!data)
        return NULL;

    results[0].of.i32 = write_wasm_bytes(state, args[2].of.i32,
                                         args[3].of.i32, data, (int32_t)len);
    free(data);
    return NULL;
}

static wasm_trap_t *host_file_exists(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmFileBindingState *state = (WasmFileBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->file_ctx || nargs < 2)
        return NULL;

    if (!file_has_root(state->file_ctx))
        return NULL;

    char path[FILE_MAX_PATH];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32,
                       path, sizeof(path)))
        return NULL;

    results[0].of.i32 = file_is_file(state->file_ctx, path) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_file_is_directory(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmFileBindingState *state = (WasmFileBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->file_ctx || nargs < 2)
        return NULL;

    if (!file_has_root(state->file_ctx))
        return NULL;

    char path[FILE_MAX_PATH];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32,
                       path, sizeof(path)))
        return NULL;

    results[0].of.i32 = file_is_directory(state->file_ctx, path) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_file_size(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmFileBindingState *state = (WasmFileBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->file_ctx || nargs < 2)
        return NULL;

    if (!file_has_root(state->file_ctx))
        return NULL;

    char path[FILE_MAX_PATH];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32,
                       path, sizeof(path)))
        return NULL;

    int64_t sz = file_size(state->file_ctx, path);
    results[0].of.i32 = (int32_t)sz;
    return NULL;
}

static wasmtime_error_t *register_file_functions(
    wasmtime_linker_t *linker, WasmFileBindingState *state)
{
    wasmtime_error_t *error = NULL;
    char name[64];

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        snprintf(name, sizeof(name), "files_list_json");
        error = define_file_func(linker, state, name, host_file_list_json,
                                 params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        snprintf(name, sizeof(name), "files_read_text");
        error = define_file_func(linker, state, name, host_file_read_text,
                                 params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        snprintf(name, sizeof(name), "files_read_binary");
        error = define_file_func(linker, state, name, host_file_read_binary,
                                 params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        snprintf(name, sizeof(name), "files_exists");
        error = define_file_func(linker, state, name, host_file_exists,
                                 params, 2, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        snprintf(name, sizeof(name), "files_is_directory");
        error = define_file_func(linker, state, name, host_file_is_directory,
                                 params, 2, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        snprintf(name, sizeof(name), "files_size");
        error = define_file_func(linker, state, name, host_file_size,
                                 params, 2, results, 1);
        if (error)
            return error;
    }

    return NULL;
}

wasmtime_error_t *wasm_file_register_state(wasmtime_linker_t *linker,
                                           WasmFileBindingState *state)
{
    if (!linker || !state || !state->file_ctx)
        return wasmtime_error_new("WASM file binding state is required");
    return register_file_functions(linker, state);
}

WasmFileBindingState *wasm_file_binding_state_create(const char *root_dir)
{
    WasmFileBindingState *state =
        (WasmFileBindingState *)calloc(1, sizeof(WasmFileBindingState));
    if (!state)
        return NULL;
    state->file_ctx = file_create(root_dir);
    if (!state->file_ctx)
    {
        fprintf(stderr, "Failed to create WASM file context\n");
        free(state);
        return NULL;
    }
    return state;
}

void wasm_file_binding_state_destroy(WasmFileBindingState *state)
{
    if (!state)
        return;
    file_destroy(state->file_ctx);
    free(state);
}

FileContext *wasm_file_context(WasmFileBindingState *state)
{
    return state ? state->file_ctx : NULL;
}

void wasm_file_binding_state_set_memory(WasmFileBindingState *state,
                                        wasmtime_context_t *store_ctx,
                                        wasmtime_memory_t *memory)
{
    if (!state || !store_ctx || !memory)
        return;
    state->store_ctx = store_ctx;
    state->memory = *memory;
    state->has_memory = true;
}

void wasm_file_binding_scope_enter(WasmFileBindingState *state)
{
    wasm_binding_scope_enter(&wasm_file_scope_owner, state);
}

void wasm_file_binding_scope_leave(WasmFileBindingState *state)
{
    wasm_binding_scope_leave(&wasm_file_scope_owner, state);
}

wasmtime_error_t *wasm_file_register(wasmtime_linker_t *linker)
{
    return wasm_file_register_state(
        linker, (WasmFileBindingState *)wasm_binding_scope_get(
                    &wasm_file_scope_owner));
}

void wasm_file_scoped_state_set_memory(wasmtime_context_t *store_ctx,
                                       wasmtime_memory_t *memory)
{
    wasm_file_binding_state_set_memory(
        (WasmFileBindingState *)wasm_binding_scope_get(&wasm_file_scope_owner),
        store_ctx, memory);
}