#include "sqlite/js_sqlite_bindings.h"
#include "sqlite/lua_sqlite_bindings.h"

#include "lauxlib.h"
#include "lualib.h"
#include "quickjs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    JSRuntime *runtime;
    JSContext *context;
    SqliteContext *sqlite;
} JsSqliteRuntime;

typedef struct
{
    lua_State *state;
    SqliteContext *sqlite;
} LuaSqliteRuntime;

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

static void js_eval_or_fail(JsSqliteRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "sqlite_binding_state_test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript SQLite isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_runtime_create(JsSqliteRuntime *runtime, const char *project_dir)
{
    JSValue global;

    memset(runtime, 0, sizeof(*runtime));
    runtime->runtime = JS_NewRuntime();
    assert(runtime->runtime);
    runtime->context = JS_NewContext(runtime->runtime);
    assert(runtime->context);
    global = JS_GetGlobalObject(runtime->context);
    assert(JS_SetPropertyStr(runtime->context, global, "sys",
                             JS_NewObject(runtime->context)) >= 0);
    JS_FreeValue(runtime->context, global);
    runtime->sqlite = js_sqlite_init(runtime->context, project_dir);
    assert(runtime->sqlite);
}

static void js_runtime_destroy(JsSqliteRuntime *runtime)
{
    js_sqlite_cleanup(runtime->sqlite);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
}

static void js_setup_value(JsSqliteRuntime *runtime, int value)
{
    char script[1024];
    snprintf(script, sizeof(script),
             "globalThis.dbHandle = sys.db.open('isolation');\n"
             "if (dbHandle < 0) throw Error('open failed');\n"
             "if (!sys.db.execute(dbHandle, 'CREATE TABLE items (value INTEGER NOT NULL)')) throw Error('create failed');\n"
             "if (sys.db.run(dbHandle, 'INSERT INTO items VALUES (?)', %d) !== 1) throw Error('insert failed');\n",
             value);
    js_eval_or_fail(runtime, script);
}

static void js_assert_total(JsSqliteRuntime *runtime, int expected)
{
    char script[768];
    snprintf(script, sizeof(script),
             "(function () {\n"
             "  const rows = sys.db.query(dbHandle, 'SELECT sum(value) AS total FROM items');\n"
             "  if (rows.length !== 1 || rows[0].total !== %d) throw Error('unexpected total');\n"
             "})();\n",
             expected);
    js_eval_or_fail(runtime, script);
}

static void test_javascript(const char *root_a, const char *root_b)
{
    JsSqliteRuntime runtime_a;
    JsSqliteRuntime runtime_b;

    remove_database(root_a);
    remove_database(root_b);
    js_runtime_create(&runtime_a, root_a);
    js_setup_value(&runtime_a, 11);
    js_runtime_create(&runtime_b, root_b);
    js_setup_value(&runtime_b, 22);
    js_assert_total(&runtime_a, 11);
    js_assert_total(&runtime_b, 22);

    js_runtime_destroy(&runtime_b);
    js_eval_or_fail(&runtime_a,
                    "if (sys.db.run(dbHandle, 'INSERT INTO items VALUES (?)', 33) !== 1) throw Error('second insert failed');");
    js_assert_total(&runtime_a, 44);
    js_runtime_destroy(&runtime_a);
    remove_database(root_b);
    remove_database(root_a);
}

static void lua_eval_or_fail(LuaSqliteRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua SQLite isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaSqliteRuntime *runtime, const char *project_dir)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");
    runtime->sqlite = lua_sqlite_init(runtime->state, project_dir);
    assert(runtime->sqlite);
}

static void lua_runtime_destroy(LuaSqliteRuntime *runtime)
{
    lua_sqlite_cleanup(runtime->sqlite);
    lua_close(runtime->state);
}

static void lua_setup_value(LuaSqliteRuntime *runtime, int value)
{
    char script[1024];
    snprintf(script, sizeof(script),
             "db_handle = sys.db.open('isolation')\n"
             "assert(db_handle >= 0)\n"
             "assert(sys.db.execute(db_handle, 'CREATE TABLE items (value INTEGER NOT NULL)'))\n"
             "assert(sys.db.run(db_handle, 'INSERT INTO items VALUES (?)', %d) == 1)\n",
             value);
    lua_eval_or_fail(runtime, script);
}

static void lua_assert_total(LuaSqliteRuntime *runtime, int expected)
{
    char script[768];
    snprintf(script, sizeof(script),
             "local rows = sys.db.query(db_handle, 'SELECT sum(value) AS total FROM items')\n"
             "assert(#rows == 1 and rows[1].total == %d)\n",
             expected);
    lua_eval_or_fail(runtime, script);
}

static void test_lua(const char *root_a, const char *root_b)
{
    LuaSqliteRuntime runtime_a;
    LuaSqliteRuntime runtime_b;

    remove_database(root_a);
    remove_database(root_b);
    lua_runtime_create(&runtime_a, root_a);
    lua_setup_value(&runtime_a, 11);
    lua_runtime_create(&runtime_b, root_b);
    lua_setup_value(&runtime_b, 22);
    lua_assert_total(&runtime_a, 11);
    lua_assert_total(&runtime_b, 22);
    lua_eval_or_fail(&runtime_a,
                     "local ok, err = pcall(sys.db.query, db_handle, 'SELECT missing FROM items')\n"
                     "assert(not ok and type(err) == 'string' and #err > 0)\n");

    lua_runtime_destroy(&runtime_b);
    lua_eval_or_fail(&runtime_a,
                     "assert(sys.db.run(db_handle, 'INSERT INTO items VALUES (?)', 33) == 1)\n");
    lua_assert_total(&runtime_a, 44);
    lua_runtime_destroy(&runtime_a);
    remove_database(root_b);
    remove_database(root_a);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    test_javascript(argv[1], argv[2]);
    test_lua(argv[1], argv[2]);
    puts("managed SQLite binding state isolation tests passed");
    return 0;
}