#include "wasm_util/wasm_memory.h"
#include "wasm_sqlite_bindings.h"
#include "sqlite_service.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct WasmSqliteBindingState
{
    SqliteContext *sqlite_ctx;
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool has_memory;
};

static bool read_wasm_str(WasmSqliteBindingState *state, int32_t ptr, int32_t len,
                          char *buffer, size_t buffer_size)
{
    if (!state || !state->store_ctx || !state->has_memory)
        return false;
    return wasm_mem_read_str(state->store_ctx, &state->memory,
                             ptr, len, buffer, buffer_size);
}

static int32_t write_wasm_str(WasmSqliteBindingState *state, int32_t ptr,
                              int32_t max_len, const char *str, int32_t str_len)
{
    if (!state || !state->store_ctx || !state->has_memory)
        return 0;
    return wasm_mem_write_str(state->store_ctx, &state->memory,
                              ptr, max_len, str, str_len);
}

static wasmtime_error_t *define_sqlite_func(
    wasmtime_linker_t *linker,
    WasmSqliteBindingState *state,
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
        linker, "env", 3, name, strlen(name), functype, callback, state, NULL);
    wasm_functype_delete(functype);
    return error;
}

static wasm_trap_t *host_db_open(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->sqlite_ctx || nargs < 2)
        return NULL;

    char name[256];
    if (!read_wasm_str(state, args[0].of.i32, args[1].of.i32, name, sizeof(name)))
        return NULL;

    ApiError error;
    results[0].of.i32 = sqlite_service_open(state->sqlite_ctx, name, &error);
    return NULL;
}

static wasm_trap_t *host_db_close(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    (void)results;
    (void)nresults;
    if (state && state->sqlite_ctx && nargs >= 1)
        sqlite_close(state->sqlite_ctx, args[0].of.i32);
    return NULL;
}

static wasm_trap_t *host_db_execute(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->sqlite_ctx || nargs < 3)
        return NULL;

    char sql[4096];
    if (!read_wasm_str(state, args[1].of.i32, args[2].of.i32, sql, sizeof(sql)))
        return NULL;

    results[0].of.i32 = sqlite_execute(state->sqlite_ctx, args[0].of.i32, sql) ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_db_run(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->sqlite_ctx || nargs < 3)
        return NULL;

    char sql[4096];
    if (!read_wasm_str(state, args[1].of.i32, args[2].of.i32, sql, sizeof(sql)))
        return NULL;

    results[0].of.i32 = sqlite_run(state->sqlite_ctx, args[0].of.i32, sql, NULL, 0);
    return NULL;
}

static wasm_trap_t *host_db_run_int(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->sqlite_ctx || nargs < 4)
        return NULL;

    char sql[4096];
    if (!read_wasm_str(state, args[1].of.i32, args[2].of.i32, sql, sizeof(sql)))
        return NULL;

    SqliteParam params[1];
    params[0].type = SQLITE_VAL_INTEGER;
    params[0].value.integer = args[3].of.i32;

    results[0].of.i32 = sqlite_run(state->sqlite_ctx, args[0].of.i32, sql, params, 1);
    return NULL;
}

static wasm_trap_t *host_db_run_str(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->sqlite_ctx || nargs < 5)
        return NULL;

    char sql[4096];
    if (!read_wasm_str(state, args[1].of.i32, args[2].of.i32, sql, sizeof(sql)))
        return NULL;

    char param[4096];
    if (!read_wasm_str(state, args[3].of.i32, args[4].of.i32, param, sizeof(param)))
        return NULL;

    SqliteParam params[1];
    params[0].type = SQLITE_VAL_TEXT;
    params[0].value.text.data = param;
    params[0].value.text.length = (int)strlen(param);

    results[0].of.i32 = sqlite_run(state->sqlite_ctx, args[0].of.i32, sql, params, 1);
    return NULL;
}

static wasm_trap_t *host_db_query_json(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->sqlite_ctx || nargs < 5)
        return NULL;

    char sql[4096];
    if (!read_wasm_str(state, args[1].of.i32, args[2].of.i32, sql, sizeof(sql)))
        return NULL;

    SqliteResult *qresult = sqlite_query(state->sqlite_ctx, args[0].of.i32, sql, NULL, 0);
    if (!qresult)
        return NULL;
    if (qresult->error)
    {
        sqlite_result_free(qresult);
        results[0].of.i32 = -1;
        return NULL;
    }

    size_t json_cap = 8192;
    char *json = (char *)malloc(json_cap);
    if (!json)
    {
        sqlite_result_free(qresult);
        return NULL;
    }

    size_t pos = 0;
    json[pos++] = '[';

    for (int r = 0; r < qresult->row_count; r++)
    {
        if (r > 0)
            json[pos++] = ',';
        json[pos++] = '{';

        SqliteRow *row = &qresult->rows[r];
        for (int c = 0; c < row->column_count; c++)
        {
            if (c > 0)
                json[pos++] = ',';

            if (pos + 512 > json_cap)
            {
                json_cap *= 2;
                json = (char *)realloc(json, json_cap);
                if (!json)
                {
                    sqlite_result_free(qresult);
                    return NULL;
                }
            }

            pos += snprintf(json + pos, json_cap - pos, "\"%s\":", row->column_names[c]);

            switch (row->values[c].type)
            {
            case SQLITE_VAL_INTEGER:
                pos += snprintf(json + pos, json_cap - pos, "%lld",
                                (long long)row->values[c].value.integer);
                break;
            case SQLITE_VAL_FLOAT:
                pos += snprintf(json + pos, json_cap - pos, "%g",
                                row->values[c].value.real);
                break;
            case SQLITE_VAL_TEXT:
            {
                
                json[pos++] = '"';
                const char *s = row->values[c].value.text.data;
                int slen = row->values[c].value.text.length;
                for (int i = 0; i < slen; i++)
                {
                    if (pos + 8 > json_cap)
                    {
                        json_cap *= 2;
                        json = (char *)realloc(json, json_cap);
                        if (!json)
                        {
                            sqlite_result_free(qresult);
                            return NULL;
                        }
                    }
                    if (s[i] == '"')
                    {
                        json[pos++] = '\\';
                        json[pos++] = '"';
                    }
                    else if (s[i] == '\\')
                    {
                        json[pos++] = '\\';
                        json[pos++] = '\\';
                    }
                    else if (s[i] == '\n')
                    {
                        json[pos++] = '\\';
                        json[pos++] = 'n';
                    }
                    else if (s[i] == '\r')
                    {
                        json[pos++] = '\\';
                        json[pos++] = 'r';
                    }
                    else if (s[i] == '\t')
                    {
                        json[pos++] = '\\';
                        json[pos++] = 't';
                    }
                    else
                        json[pos++] = s[i];
                }
                json[pos++] = '"';
                break;
            }
            default:
                pos += snprintf(json + pos, json_cap - pos, "null");
                break;
            }
        }
        json[pos++] = '}';
    }
    json[pos++] = ']';
    json[pos] = '\0';

    sqlite_result_free(qresult);

    results[0].of.i32 = write_wasm_str(state, args[3].of.i32, args[4].of.i32,
                                       json, (int32_t)pos);
    free(json);

    return NULL;
}

static wasm_trap_t *host_db_last_insert_id(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmSqliteBindingState *state = (WasmSqliteBindingState *)env;
    (void)caller;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (state && state->sqlite_ctx && nargs >= 1)
        results[0].of.i32 = (int32_t)sqlite_last_insert_id(state->sqlite_ctx,
                                                           args[0].of.i32);

    return NULL;
}

wasmtime_error_t *wasm_sqlite_register(wasmtime_linker_t *linker,
                                       WasmSqliteBindingState *state)
{
    wasmtime_error_t *error = NULL;

    if (!linker || !state || !state->sqlite_ctx)
        return wasmtime_error_new("WASM SQLite binding state is required");

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_open", host_db_open, params, 2, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_close", host_db_close, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_execute", host_db_execute, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_run", host_db_run, params, 3, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_run_int", host_db_run_int, params, 4, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_run_str", host_db_run_str, params, 5, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_query_json", host_db_query_json, params, 5, results, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_sqlite_func(linker, state, "db_last_insert_id", host_db_last_insert_id, params, 1, results, 1);
        if (error)
            return error;
    }

    return NULL;
}

WasmSqliteBindingState *wasm_sqlite_binding_state_create(const char *project_dir)
{
    WasmSqliteBindingState *state =
        (WasmSqliteBindingState *)calloc(1, sizeof(WasmSqliteBindingState));
    if (!state)
        return NULL;

    state->sqlite_ctx = sqlite_create(project_dir);
    if (!state->sqlite_ctx)
    {
        fprintf(stderr, "Failed to create WASM SQLite context\n");
        free(state);
        return NULL;
    }
    return state;
}

void wasm_sqlite_binding_state_destroy(WasmSqliteBindingState *state)
{
    if (!state)
        return;
    sqlite_destroy(state->sqlite_ctx);
    free(state);
}

void wasm_sqlite_set_memory(WasmSqliteBindingState *state,
                            wasmtime_context_t *store_ctx,
                            wasmtime_memory_t *memory)
{
    if (!state || !store_ctx || !memory)
        return;
    state->store_ctx = store_ctx;
    state->memory = *memory;
    state->has_memory = true;
}