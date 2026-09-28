#include "file/js_file_bindings.h"
#include "file/lua_file_bindings.h"
#include "file/wasm_file_bindings.h"
#include "sqlite/js_sqlite_bindings.h"
#include "sqlite/lua_sqlite_bindings.h"
#include "sqlite/wasm_sqlite_bindings.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasmtime.h>

#include <lauxlib.h>
#include <lualib.h>
#include <quickjs.h>

#define TRACE_COUNT 7
#define TRACE_RESULT_CAPACITY 256

bool budo_test_file_pick_text(JsFileBridgeToken token, const char *extension)
{
    (void)token;
    (void)extension;
    return false;
}

bool budo_test_file_save_text(JsFileBridgeToken token, const char *name,
                              const char *text)
{
    (void)token;
    (void)name;
    (void)text;
    return false;
}

static const char *TRACE_OPERATIONS[TRACE_COUNT] = {
    "sqlite.insertChanges",
    "sqlite.query",
    "sqlite.invalidQuery",
    "file.readText",
    "file.exists",
    "file.missing",
    "file.invalidPath",
};

typedef struct TraceRecord
{
    const char *operation;
    char result[TRACE_RESULT_CAPACITY];
    char error_kind[32];
    char error_code[64];
} TraceRecord;

typedef struct RuntimeTrace
{
    TraceRecord records[TRACE_COUNT];
} RuntimeTrace;

static void trace_init(RuntimeTrace *trace)
{
    memset(trace, 0, sizeof(*trace));
    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        trace->records[index].operation = TRACE_OPERATIONS[index];
        strcpy(trace->records[index].result, "null");
        strcpy(trace->records[index].error_kind, "none");
    }
}

static void trace_result(RuntimeTrace *trace, size_t index, const char *json)
{
    assert(strlen(json) < sizeof(trace->records[index].result));
    strcpy(trace->records[index].result, json);
}

static void trace_error(RuntimeTrace *trace, size_t index,
                        const char *kind, const char *code)
{
    assert(strlen(kind) < sizeof(trace->records[index].error_kind));
    assert(strlen(code) < sizeof(trace->records[index].error_code));
    strcpy(trace->records[index].result, "null");
    strcpy(trace->records[index].error_kind, kind);
    strcpy(trace->records[index].error_code, code);
}

static void trace_print(const char *runtime, const TraceRecord *record)
{
    printf("{\"runtime\":\"%s\",\"operation\":\"%s\","
           "\"result\":%s,\"errorKind\":\"%s\","
           "\"errorCode\":\"%s\"}\n",
           runtime, record->operation, record->result,
           record->error_kind, record->error_code);
}

static void trace_compare(const char *expected_runtime,
                          const RuntimeTrace *expected,
                          const char *actual_runtime,
                          const RuntimeTrace *actual)
{
    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        const TraceRecord *left = &expected->records[index];
        const TraceRecord *right = &actual->records[index];
        if (strcmp(left->operation, right->operation) != 0 ||
            strcmp(left->result, right->result) != 0 ||
            strcmp(left->error_kind, right->error_kind) != 0 ||
            strcmp(left->error_code, right->error_code) != 0)
        {
            fprintf(stderr, "Conformance mismatch between %s and %s:\n",
                    expected_runtime, actual_runtime);
            trace_print(expected_runtime, left);
            trace_print(actual_runtime, right);
            abort();
        }
    }
}

static void remove_database(const char *root)
{
    static const char *suffixes[] = {".db", ".db-shm", ".db-wal", ".db-journal"};
    char path[1024];
    for (size_t index = 0; index < sizeof(suffixes) / sizeof(suffixes[0]); index++)
    {
        snprintf(path, sizeof(path), "%s/conformance%s", root, suffixes[index]);
        remove(path);
    }
}

static void write_fixture(const char *root)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/identity.txt", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs("shared-content", file) >= 0);
    assert(fclose(file) == 0);
}

static void remove_fixture(const char *root)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/identity.txt", root);
    remove(path);
}

static void js_fail(JSContext *context, const char *label)
{
    JSValue exception = JS_GetException(context);
    const char *message = JS_ToCString(context, exception);
    fprintf(stderr, "%s: %s\n", label, message ? message : "unknown error");
    JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    abort();
}

static void js_eval(JSContext *context, const char *script)
{
    JSValue result = JS_Eval(context, script, strlen(script),
                             "sqlite-file-conformance.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
        js_fail(context, "JavaScript conformance script");
    JS_FreeValue(context, result);
}

static int js_global_int(JSContext *context, const char *name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    int32_t result = 0;
    assert(JS_ToInt32(context, &result, value) == 0);
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return result;
}

static bool js_global_bool(JSContext *context, const char *name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    bool result = JS_ToBool(context, value) == 1;
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return result;
}

static char *js_global_string(JSContext *context, const char *name)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue value = JS_GetPropertyStr(context, global, name);
    const char *text = JS_ToCString(context, value);
    char *result = text ? strdup(text) : strdup("");
    JS_FreeCString(context, text);
    JS_FreeValue(context, value);
    JS_FreeValue(context, global);
    return result;
}

static void run_javascript_trace(const char *root, RuntimeTrace *trace)
{
    static const char script[] =
        "const handle = sys.db.open('conformance');\n"
        "if (handle < 0) throw Error('open failed');\n"
        "if (!sys.db.execute(handle, 'CREATE TABLE items (value INTEGER NOT NULL)')) throw Error('create failed');\n"
        "globalThis.insertChanges = sys.db.run(handle, 'INSERT INTO items VALUES (?)', 7);\n"
        "globalThis.queryResult = JSON.stringify(sys.db.query(handle, 'SELECT value FROM items'));\n"
        "try { sys.db.query(handle, 'SELECT missing FROM items'); }"
        " catch (error) { globalThis.invalidQuery = true; }\n"
        "globalThis.fileText = sys.assets.readText('identity.txt');\n"
        "globalThis.fileExists = sys.assets.exists('identity.txt');\n"
        "globalThis.fileMissing = sys.assets.readText('missing.txt') === null;\n"
        "try { sys.files.readText('../escape.txt'); }"
        " catch (error) { globalThis.invalidPath = true; }\n";
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context;
    SqliteContext *sqlite;
    JsFileContext *files;
    JSValue global;
    char *text;
    char *query;

    assert(runtime);
    context = JS_NewContext(runtime);
    assert(context);
    global = JS_GetGlobalObject(context);
    assert(JS_SetPropertyStr(context, global, "sys", JS_NewObject(context)) >= 0);
    JS_FreeValue(context, global);
    sqlite = js_sqlite_init(context, root);
    files = js_file_init(context, root);
    assert(sqlite && files);
    js_eval(context, script);

    trace_init(trace);
    snprintf(trace->records[0].result, TRACE_RESULT_CAPACITY, "%d",
             js_global_int(context, "insertChanges"));
    query = js_global_string(context, "queryResult");
    trace_result(trace, 1, query);
    free(query);
    assert(js_global_bool(context, "invalidQuery"));
    trace_error(trace, 2, "programmer", "sqlite.query_failed");
    text = js_global_string(context, "fileText");
    snprintf(trace->records[3].result, TRACE_RESULT_CAPACITY, "\"%s\"", text);
    free(text);
    trace_result(trace, 4, js_global_bool(context, "fileExists") ? "true" : "false");
    trace_result(trace, 5, js_global_bool(context, "fileMissing") ? "true" : "false");
    assert(js_global_bool(context, "invalidPath"));
    trace_error(trace, 6, "programmer", "file.invalid_path");

    js_file_cleanup(files);
    js_sqlite_cleanup(sqlite);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

static double lua_global_number(lua_State *state, const char *name)
{
    lua_getglobal(state, name);
    assert(lua_isnumber(state, -1));
    double result = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return result;
}

static bool lua_global_bool(lua_State *state, const char *name)
{
    lua_getglobal(state, name);
    bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

static const char *lua_global_string(lua_State *state, const char *name)
{
    lua_getglobal(state, name);
    const char *result = lua_tostring(state, -1);
    assert(result);
    return result;
}

static void run_lua_trace(const char *root, RuntimeTrace *trace)
{
    static const char script[] =
        "local handle = sys.db.open('conformance')\n"
        "assert(handle >= 0)\n"
        "assert(sys.db.execute(handle, 'CREATE TABLE items (value INTEGER NOT NULL)'))\n"
        "insert_changes = sys.db.run(handle, 'INSERT INTO items VALUES (?)', 7)\n"
        "local rows = sys.db.query(handle, 'SELECT value FROM items')\n"
        "query_value = rows[1].value\n"
        "local ok = pcall(sys.db.query, handle, 'SELECT missing FROM items')\n"
        "invalid_query = not ok\n"
        "file_text = sys.assets.readText('identity.txt')\n"
        "file_exists = sys.assets.exists('identity.txt')\n"
        "file_missing = sys.assets.readText('missing.txt') == nil\n"
        "ok = pcall(sys.files.readText, '../escape.txt')\n"
        "invalid_path = not ok\n";
    lua_State *state = luaL_newstate();
    SqliteContext *sqlite;
    FileContext *files;
    const char *text;

    assert(state);
    luaL_openlibs(state);
    lua_newtable(state);
    lua_setglobal(state, "sys");
    sqlite = lua_sqlite_init(state, root);
    files = lua_file_init(state, root);
    assert(sqlite && files);
    if (luaL_dostring(state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua conformance script: %s\n", lua_tostring(state, -1));
        abort();
    }

    trace_init(trace);
    snprintf(trace->records[0].result, TRACE_RESULT_CAPACITY, "%.0f",
             lua_global_number(state, "insert_changes"));
    snprintf(trace->records[1].result, TRACE_RESULT_CAPACITY,
             "[{\"value\":%.0f}]", lua_global_number(state, "query_value"));
    assert(lua_global_bool(state, "invalid_query"));
    trace_error(trace, 2, "programmer", "sqlite.query_failed");
    text = lua_global_string(state, "file_text");
    snprintf(trace->records[3].result, TRACE_RESULT_CAPACITY, "\"%s\"", text);
    lua_pop(state, 1);
    trace_result(trace, 4, lua_global_bool(state, "file_exists") ? "true" : "false");
    trace_result(trace, 5, lua_global_bool(state, "file_missing") ? "true" : "false");
    assert(lua_global_bool(state, "invalid_path"));
    trace_error(trace, 6, "programmer", "file.invalid_path");

    lua_file_cleanup(files);
    lua_sqlite_cleanup(sqlite);
    lua_close(state);
}

typedef struct WasmRuntime
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmSqliteBindingState *sqlite;
    WasmFileBindingState *files;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
} WasmRuntime;

static const char WASM_MODULE[] =
    "(module\n"
    " (import \"env\" \"db_open\" (func $open (param i32 i32) (result i32)))\n"
    " (import \"env\" \"db_execute\" (func $execute (param i32 i32 i32) (result i32)))\n"
    " (import \"env\" \"db_run_int\" (func $run (param i32 i32 i32 i32) (result i32)))\n"
    " (import \"env\" \"db_query_json\" (func $query (param i32 i32 i32 i32 i32) (result i32)))\n"
    " (import \"env\" \"files_read_text\" (func $read (param i32 i32 i32 i32) (result i32)))\n"
    " (import \"env\" \"files_exists\" (func $exists (param i32 i32) (result i32)))\n"
    " (memory (export \"memory\") 1)\n"
    " (export \"open\" (func $open))\n"
    " (export \"execute\" (func $execute))\n"
    " (export \"run\" (func $run))\n"
    " (export \"query\" (func $query))\n"
    " (export \"read\" (func $read))\n"
    " (export \"exists\" (func $exists)))";

static void wasm_fail_error(wasmtime_error_t *error)
{
    wasm_byte_vec_t message;
    wasmtime_error_message(error, &message);
    fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    abort();
}

static void wasm_fail_trap(wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    wasm_trap_message(trap, &message);
    fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
    abort();
}

static wasmtime_func_t wasm_function(WasmRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    bool found = wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                              name, strlen(name), &exported);
    assert(found && exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static int32_t wasm_call(WasmRuntime *runtime, wasmtime_func_t *function,
                         wasmtime_val_t *args, size_t count)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 args, count, &result, 1, &trap);
    if (error)
        wasm_fail_error(error);
    if (trap)
        wasm_fail_trap(trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static int32_t wasm_write(WasmRuntime *runtime, int32_t offset, const char *text)
{
    size_t length = strlen(text);
    assert((size_t)offset + length < wasmtime_memory_data_size(
                                         runtime->context, &runtime->memory));
    memcpy(wasmtime_memory_data(runtime->context, &runtime->memory) + offset,
           text, length);
    return (int32_t)length;
}

static int32_t wasm_call_string(WasmRuntime *runtime, wasmtime_func_t *function,
                                int32_t offset, const char *text)
{
    wasmtime_val_t args[2];
    int32_t length = wasm_write(runtime, offset, text);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = offset;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = length;
    return wasm_call(runtime, function, args, 2);
}

static int32_t wasm_sql(WasmRuntime *runtime, wasmtime_func_t *function,
                        int32_t handle, const char *sql)
{
    wasmtime_val_t args[3];
    int32_t length = wasm_write(runtime, 256, sql);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = 256;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = length;
    return wasm_call(runtime, function, args, 3);
}

static void run_wasmtime_trace(const char *root, RuntimeTrace *trace)
{
    WasmRuntime runtime;
    wasm_byte_vec_t wasm;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;
    wasmtime_extern_t memory;
    bool found;
    wasmtime_func_t open, execute, run, query, read, exists;
    wasmtime_val_t args[5];
    int32_t handle, written;
    char output[256];

    memset(&runtime, 0, sizeof(runtime));
    runtime.engine = wasm_engine_new();
    runtime.store = wasmtime_store_new(runtime.engine, NULL, NULL);
    runtime.context = wasmtime_store_context(runtime.store);
    runtime.linker = wasmtime_linker_new(runtime.engine);
    runtime.sqlite = wasm_sqlite_binding_state_create(root);
    runtime.files = wasm_file_binding_state_create(root);
    assert(runtime.engine && runtime.store && runtime.linker && runtime.sqlite && runtime.files);
    error = wasm_sqlite_register(runtime.linker, runtime.sqlite);
    if (error)
        wasm_fail_error(error);
    error = wasm_file_register_state(runtime.linker, runtime.files);
    if (error)
        wasm_fail_error(error);
    error = wasmtime_wat2wasm(WASM_MODULE, strlen(WASM_MODULE), &wasm);
    if (error)
        wasm_fail_error(error);
    error = wasmtime_module_new(runtime.engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime.module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        wasm_fail_error(error);
    error = wasmtime_linker_instantiate(runtime.linker, runtime.context,
                                        runtime.module, &runtime.instance, &trap);
    if (error)
        wasm_fail_error(error);
    if (trap)
        wasm_fail_trap(trap);
    found = wasmtime_instance_export_get(runtime.context, &runtime.instance,
                                         "memory", 6, &memory);
    assert(found);
    assert(memory.kind == WASMTIME_EXTERN_MEMORY);
    runtime.memory = memory.of.memory;
    wasm_sqlite_set_memory(runtime.sqlite, runtime.context, &runtime.memory);
    wasm_file_binding_state_set_memory(runtime.files, runtime.context, &runtime.memory);
    open = wasm_function(&runtime, "open");
    execute = wasm_function(&runtime, "execute");
    run = wasm_function(&runtime, "run");
    query = wasm_function(&runtime, "query");
    read = wasm_function(&runtime, "read");
    exists = wasm_function(&runtime, "exists");

    handle = wasm_call_string(&runtime, &open, 0, "conformance");
    assert(handle >= 0);
    assert(wasm_sql(&runtime, &execute, handle,
                    "CREATE TABLE items (value INTEGER NOT NULL)") == 1);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = 256;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = wasm_write(&runtime, 256, "INSERT INTO items VALUES (?)");
    args[3].kind = WASMTIME_I32;
    args[3].of.i32 = 7;
    assert(wasm_call(&runtime, &run, args, 4) == 1);

    args[0].of.i32 = handle;
    args[1].of.i32 = 256;
    args[2].of.i32 = wasm_write(&runtime, 256, "SELECT value FROM items");
    args[3].kind = WASMTIME_I32;
    args[3].of.i32 = 8192;
    args[4].kind = WASMTIME_I32;
    args[4].of.i32 = sizeof(output);
    written = wasm_call(&runtime, &query, args, 5);
    assert(written > 0);
    memcpy(output, wasmtime_memory_data(runtime.context, &runtime.memory) + 8192,
           (size_t)written);
    output[written] = '\0';

    trace_init(trace);
    trace_result(trace, 0, "1");
    trace_result(trace, 1, output);
    args[2].of.i32 = wasm_write(&runtime, 256, "SELECT missing FROM items");
    assert(wasm_call(&runtime, &query, args, 5) == -1);
    trace_error(trace, 2, "programmer", "sqlite.query_failed");

    args[0].of.i32 = 0;
    args[1].of.i32 = wasm_write(&runtime, 0, "assets/identity.txt");
    args[2].of.i32 = 12288;
    args[3].of.i32 = sizeof(output);
    written = wasm_call(&runtime, &read, args, 4);
    assert(written > 0);
    memcpy(output, wasmtime_memory_data(runtime.context, &runtime.memory) + 12288,
           (size_t)written);
    output[written] = '\0';
    snprintf(trace->records[3].result, TRACE_RESULT_CAPACITY, "\"%s\"", output);
    assert(wasm_call_string(&runtime, &exists, 0, "assets/identity.txt") == 1);
    trace_result(trace, 4, "true");
    args[0].of.i32 = 0;
    args[1].of.i32 = wasm_write(&runtime, 0, "assets/missing.txt");
    args[2].of.i32 = 12288;
    args[3].of.i32 = sizeof(output);
    assert(wasm_call(&runtime, &read, args, 4) == -1);
    trace_result(trace, 5, "true");
    args[0].of.i32 = 0;
    args[1].of.i32 = wasm_write(&runtime, 0, "../escape.txt");
    assert(wasm_call(&runtime, &read, args, 4) == -1);
    trace_error(trace, 6, "programmer", "file.invalid_path");

    wasmtime_module_delete(runtime.module);
    wasmtime_linker_delete(runtime.linker);
    wasmtime_store_delete(runtime.store);
    wasm_file_binding_state_destroy(runtime.files);
    wasm_sqlite_binding_state_destroy(runtime.sqlite);
    wasm_engine_delete(runtime.engine);
}

int main(int argc, char **argv)
{
    RuntimeTrace javascript, lua, wasmtime;
    const char *root;

    assert(argc == 2);
    root = argv[1];
    remove_database(root);
    write_fixture(root);
    run_javascript_trace(root, &javascript);
    remove_database(root);
    run_lua_trace(root, &lua);
    remove_database(root);
    run_wasmtime_trace(root, &wasmtime);
    trace_compare("javascript", &javascript, "lua", &lua);
    trace_compare("javascript", &javascript, "wasmtime", &wasmtime);
    for (size_t index = 0; index < TRACE_COUNT; index++)
    {
        trace_print("javascript", &javascript.records[index]);
        trace_print("lua", &lua.records[index]);
        trace_print("wasmtime", &wasmtime.records[index]);
    }
    remove_database(root);
    remove_fixture(root);
    puts("SQLite/file runtime conformance tests passed");
    return 0;
}