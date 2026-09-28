#include "sqlite/wasm_sqlite_bindings.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasmtime.h>

typedef struct
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmSqliteBindingState *sqlite_state;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
    wasmtime_func_t open;
    wasmtime_func_t execute;
    wasmtime_func_t run_int;
    wasmtime_func_t query_json;
} SqliteRuntime;

static const char SQLITE_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"db_open\" (func $db_open (param i32 i32) (result i32)))\n"
    "  (import \"env\" \"db_execute\" (func $db_execute (param i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"db_run_int\" (func $db_run_int (param i32 i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"db_query_json\" (func $db_query_json (param i32 i32 i32 i32 i32) (result i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (export \"open\" (func $db_open))\n"
    "  (export \"execute\" (func $db_execute))\n"
    "  (export \"run_int\" (func $db_run_int))\n"
    "  (export \"query_json\" (func $db_query_json)))";

static void fail_wasmtime_error(wasmtime_error_t *error)
{
    wasm_byte_vec_t message;
    wasmtime_error_message(error, &message);
    fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    assert(0);
}

static void fail_wasm_trap(wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    wasm_trap_message(trap, &message);
    fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
    assert(0);
}

static wasmtime_func_t get_function(SqliteRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        name, strlen(name), &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void sqlite_runtime_create(SqliteRuntime *runtime, const char *project_dir)
{
    wasm_byte_vec_t wasm;
    wasmtime_extern_t exported;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;

    memset(runtime, 0, sizeof(*runtime));
    runtime->engine = wasm_engine_new();
    assert(runtime->engine);
    runtime->store = wasmtime_store_new(runtime->engine, NULL, NULL);
    assert(runtime->store);
    runtime->context = wasmtime_store_context(runtime->store);
    runtime->linker = wasmtime_linker_new(runtime->engine);
    assert(runtime->linker);
    runtime->sqlite_state = wasm_sqlite_binding_state_create(project_dir);
    assert(runtime->sqlite_state);

    error = wasm_sqlite_register(runtime->linker, runtime->sqlite_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(SQLITE_MODULE_WAT, strlen(SQLITE_MODULE_WAT), &wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_module_new(runtime->engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime->module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_linker_instantiate(runtime->linker, runtime->context,
                                        runtime->module, &runtime->instance, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);

    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        "memory", 6, &exported));
    assert(exported.kind == WASMTIME_EXTERN_MEMORY);
    runtime->memory = exported.of.memory;
    wasm_sqlite_set_memory(runtime->sqlite_state, runtime->context, &runtime->memory);
    runtime->open = get_function(runtime, "open");
    runtime->execute = get_function(runtime, "execute");
    runtime->run_int = get_function(runtime, "run_int");
    runtime->query_json = get_function(runtime, "query_json");
}

static void sqlite_runtime_destroy(SqliteRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_sqlite_binding_state_destroy(runtime->sqlite_state);
    wasm_engine_delete(runtime->engine);
}

static int32_t write_string(SqliteRuntime *runtime, int32_t offset, const char *value)
{
    size_t length = strlen(value);
    assert(offset >= 0);
    assert((size_t)offset + length + 1 <=
           wasmtime_memory_data_size(runtime->context, &runtime->memory));
    memcpy(wasmtime_memory_data(runtime->context, &runtime->memory) + offset,
           value, length + 1);
    return (int32_t)length;
}

static int32_t call_i32(SqliteRuntime *runtime, wasmtime_func_t *function,
                        wasmtime_val_t *args, size_t nargs)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 args, nargs, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static int32_t open_database(SqliteRuntime *runtime, const char *name)
{
    wasmtime_val_t args[2];
    int32_t length = write_string(runtime, 0, name);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = 0;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = length;
    return call_i32(runtime, &runtime->open, args, 2);
}

static int32_t execute_sql(SqliteRuntime *runtime, int32_t handle, const char *sql)
{
    wasmtime_val_t args[3];
    int32_t length = write_string(runtime, 128, sql);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = 128;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = length;
    return call_i32(runtime, &runtime->execute, args, 3);
}

static int32_t run_int(SqliteRuntime *runtime, int32_t handle,
                       const char *sql, int32_t value)
{
    wasmtime_val_t args[4];
    int32_t length = write_string(runtime, 128, sql);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = 128;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = length;
    args[3].kind = WASMTIME_I32;
    args[3].of.i32 = value;
    return call_i32(runtime, &runtime->run_int, args, 4);
}

static int32_t query_json(SqliteRuntime *runtime, int32_t handle,
                          const char *sql, char *output, size_t output_size)
{
    static const int32_t output_offset = 8192;
    wasmtime_val_t args[5];
    int32_t length = write_string(runtime, 128, sql);
    int32_t written;

    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = 128;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = length;
    args[3].kind = WASMTIME_I32;
    args[3].of.i32 = output_offset;
    args[4].kind = WASMTIME_I32;
    args[4].of.i32 = (int32_t)output_size;
    written = call_i32(runtime, &runtime->query_json, args, 5);
    if (written >= 0)
    {
        assert((size_t)written < output_size);
        memcpy(output,
               wasmtime_memory_data(runtime->context, &runtime->memory) + output_offset,
               (size_t)written + 1);
    }
    return written;
}

static void remove_database(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/isolation.db", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/isolation.db-shm", dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/isolation.db-wal", dir);
    remove(path);
}

int main(int argc, char **argv)
{
    const char *root_a;
    const char *root_b;
    char output[256];
    SqliteRuntime runtime_a;
    SqliteRuntime runtime_b;
    int32_t handle_a;
    int32_t handle_b;

    assert(argc == 3);
    root_a = argv[1];
    root_b = argv[2];
    remove_database(root_a);
    remove_database(root_b);
    sqlite_runtime_create(&runtime_a, root_a);
    sqlite_runtime_create(&runtime_b, root_b);

    handle_a = open_database(&runtime_a, "isolation");
    handle_b = open_database(&runtime_b, "isolation");
    assert(handle_a >= 0);
    assert(handle_b >= 0);
    assert(execute_sql(&runtime_a, handle_a,
                       "CREATE TABLE items (value INTEGER NOT NULL)") == 1);
    assert(execute_sql(&runtime_b, handle_b,
                       "CREATE TABLE items (value INTEGER NOT NULL)") == 1);
    assert(run_int(&runtime_a, handle_a, "INSERT INTO items VALUES (?)", 11) == 1);
    assert(run_int(&runtime_b, handle_b, "INSERT INTO items VALUES (?)", 22) == 1);

    assert(query_json(&runtime_a, handle_a, "SELECT value FROM items",
                      output, sizeof(output)) > 0);
    assert(strcmp(output, "[{\"value\":11}]") == 0);
    assert(query_json(&runtime_b, handle_b, "SELECT value FROM items",
                      output, sizeof(output)) > 0);
    assert(strcmp(output, "[{\"value\":22}]") == 0);
    assert(query_json(&runtime_a, handle_a, "SELECT missing FROM items",
                      output, sizeof(output)) == -1);

    sqlite_runtime_destroy(&runtime_b);
    assert(run_int(&runtime_a, handle_a, "INSERT INTO items VALUES (?)", 33) == 1);
    assert(query_json(&runtime_a, handle_a, "SELECT sum(value) AS total FROM items",
                      output, sizeof(output)) > 0);
    assert(strcmp(output, "[{\"total\":44}]") == 0);
    sqlite_runtime_destroy(&runtime_a);

    remove_database(root_b);
    remove_database(root_a);
    puts("WASM SQLite binding state isolation test passed");
    return 0;
}