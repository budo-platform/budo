#include "js_sqlite_bindings.h"
#include "sqlite_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SqliteContext *js_db_context(JSContext *ctx, JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    SqliteContext *sqlite_ctx = NULL;

    if (data && size == sizeof(sqlite_ctx))
        memcpy(&sqlite_ctx, data, sizeof(sqlite_ctx));
    return sqlite_ctx;
}

static JSValue js_db_open(JSContext *ctx, JSValueConst this_val, int argc,
                          JSValueConst *argv, int magic, JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_NewInt32(ctx, -1);

    ApiError error;
    int handle = sqlite_service_open(sqlite_ctx, name, &error);
    JS_FreeCString(ctx, name);

    if (handle < 0)
    {
        return JS_ThrowInternalError(ctx, "%s", sqlite_get_error(sqlite_ctx));
    }

    return JS_NewInt32(ctx, handle);
}

static JSValue js_db_close(JSContext *ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv, int magic, JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx || argc < 1)
        return JS_UNDEFINED;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);
    sqlite_close(sqlite_ctx, handle);
    return JS_UNDEFINED;
}

static JSValue js_db_execute(JSContext *ctx, JSValueConst this_val, int argc,
                             JSValueConst *argv, int magic, JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx || argc < 2)
        return JS_FALSE;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    const char *sql = JS_ToCString(ctx, argv[1]);
    if (!sql)
        return JS_FALSE;

    bool ok = sqlite_execute(sqlite_ctx, handle, sql);
    JS_FreeCString(ctx, sql);

    if (!ok)
    {
        return JS_ThrowInternalError(ctx, "%s", sqlite_get_error(sqlite_ctx));
    }

    return JS_TRUE;
}

static int extract_params(JSContext *ctx, int argc, JSValueConst *argv, int start_index,
                          SqliteParam *params, int max_params)
{
    int count = 0;

    for (int i = start_index; i < argc && count < max_params; i++)
    {
        if (JS_IsNull(argv[i]) || JS_IsUndefined(argv[i]))
        {
            params[count].type = SQLITE_VAL_NULL;
        }
        else if (JS_IsBool(argv[i]))
        {
            params[count].type = SQLITE_VAL_INTEGER;
            params[count].value.integer = JS_ToBool(ctx, argv[i]) ? 1 : 0;
        }
        else if (JS_IsNumber(argv[i]))
        {
            
            int64_t int_val;
            double float_val;
            if (JS_ToInt64(ctx, &int_val, argv[i]) == 0)
            {
                
                JS_ToFloat64(ctx, &float_val, argv[i]);
                if ((double)int_val == float_val)
                {
                    params[count].type = SQLITE_VAL_INTEGER;
                    params[count].value.integer = int_val;
                }
                else
                {
                    params[count].type = SQLITE_VAL_FLOAT;
                    params[count].value.real = float_val;
                }
            }
            else
            {
                JS_ToFloat64(ctx, &float_val, argv[i]);
                params[count].type = SQLITE_VAL_FLOAT;
                params[count].value.real = float_val;
            }
        }
        else if (JS_IsString(argv[i]))
        {
            const char *str = JS_ToCString(ctx, argv[i]);
            params[count].type = SQLITE_VAL_TEXT;
            params[count].value.text.data = str; 
            params[count].value.text.length = str ? (int)strlen(str) : 0;
        }
        else
        {
            params[count].type = SQLITE_VAL_NULL;
        }
        count++;
    }

    return count;
}

static void free_params(JSContext *ctx, SqliteParam *params, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (params[i].type == SQLITE_VAL_TEXT && params[i].value.text.data)
        {
            JS_FreeCString(ctx, params[i].value.text.data);
        }
    }
}

static JSValue js_db_run(JSContext *ctx, JSValueConst this_val, int argc,
                         JSValueConst *argv, int magic, JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx || argc < 2)
        return JS_NewInt32(ctx, -1);

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    const char *sql = JS_ToCString(ctx, argv[1]);
    if (!sql)
        return JS_NewInt32(ctx, -1);

    SqliteParam params[SQLITE_MAX_PARAMS];
    int param_count = extract_params(ctx, argc, argv, 2, params, SQLITE_MAX_PARAMS);

    int changes = sqlite_run(sqlite_ctx, handle, sql, params, param_count);

    free_params(ctx, params, param_count);
    JS_FreeCString(ctx, sql);

    if (changes < 0)
    {
        return JS_ThrowInternalError(ctx, "%s", sqlite_get_error(sqlite_ctx));
    }

    return JS_NewInt32(ctx, changes);
}

static JSValue result_to_js(JSContext *ctx, SqliteResult *result)
{
    if (!result)
        return JS_NewArray(ctx);

    if (result->error)
    {
        JSValue err = JS_ThrowInternalError(ctx, "%s", result->error);
        sqlite_result_free(result);
        return err;
    }

    JSValue arr = JS_NewArray(ctx);

    for (int r = 0; r < result->row_count; r++)
    {
        JSValue obj = JS_NewObject(ctx);
        SqliteRow *row = &result->rows[r];

        for (int c = 0; c < row->column_count; c++)
        {
            JSValue val;
            switch (row->values[c].type)
            {
            case SQLITE_VAL_INTEGER:
                val = JS_NewInt64(ctx, row->values[c].value.integer);
                break;
            case SQLITE_VAL_FLOAT:
                val = JS_NewFloat64(ctx, row->values[c].value.real);
                break;
            case SQLITE_VAL_TEXT:
                val = JS_NewStringLen(ctx, row->values[c].value.text.data,
                                      row->values[c].value.text.length);
                break;
            default:
                val = JS_NULL;
                break;
            }
            JS_SetPropertyStr(ctx, obj, row->column_names[c], val);
        }

        JS_SetPropertyUint32(ctx, arr, r, obj);
    }

    sqlite_result_free(result);
    return arr;
}

static JSValue js_db_query(JSContext *ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv, int magic, JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx || argc < 2)
        return JS_NewArray(ctx);

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    const char *sql = JS_ToCString(ctx, argv[1]);
    if (!sql)
        return JS_NewArray(ctx);

    SqliteParam params[SQLITE_MAX_PARAMS];
    int param_count = extract_params(ctx, argc, argv, 2, params, SQLITE_MAX_PARAMS);

    SqliteResult *result = sqlite_query(sqlite_ctx, handle, sql, params, param_count);

    free_params(ctx, params, param_count);
    JS_FreeCString(ctx, sql);

    return result_to_js(ctx, result);
}

static JSValue js_db_last_insert_id(JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv, int magic,
                                    JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx || argc < 1)
        return JS_NewInt32(ctx, 0);

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    int64_t id = sqlite_last_insert_id(sqlite_ctx, handle);
    return JS_NewInt64(ctx, id);
}

static JSValue js_db_get_error(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv, int magic,
                               JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    SqliteContext *sqlite_ctx = js_db_context(ctx, func_data);
    if (!sqlite_ctx)
        return JS_NewString(ctx, "");
    return JS_NewString(ctx, sqlite_get_error(sqlite_ctx));
}

typedef struct JsDbFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsDbFunction;

static const JsDbFunction js_db_funcs[] = {
    {"open", 1, js_db_open},
    {"close", 1, js_db_close},
    {"execute", 2, js_db_execute},
    {"run", 2, js_db_run},
    {"query", 2, js_db_query},
    {"lastInsertId", 1, js_db_last_insert_id},
    {"getError", 0, js_db_get_error},
};

static int js_db_add_function(JSContext *ctx, JSValue db_obj,
                              const JsDbFunction *definition,
                              SqliteContext *sqlite_ctx)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&sqlite_ctx,
                                         sizeof(sqlite_ctx));
    if (JS_IsException(data))
        return -1;

    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;

    return JS_SetPropertyStr(ctx, db_obj, definition->name, function);
}

SqliteContext *js_sqlite_init(JSContext *ctx, const char *project_dir)
{
    SqliteContext *sqlite_ctx = sqlite_create(project_dir);
    if (!sqlite_ctx)
    {
        fprintf(stderr, "Failed to create SQLite context\n");
        return NULL;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");

    JSValue db_obj = JS_NewObject(ctx);
    for (size_t index = 0; index < sizeof(js_db_funcs) / sizeof(js_db_funcs[0]); index++)
    {
        if (js_db_add_function(ctx, db_obj, &js_db_funcs[index], sqlite_ctx) < 0)
        {
            JS_FreeValue(ctx, db_obj);
            JS_FreeValue(ctx, sys_obj);
            JS_FreeValue(ctx, global);
            sqlite_destroy(sqlite_ctx);
            return NULL;
        }
    }

    JS_SetPropertyStr(ctx, sys_obj, "db", db_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    return sqlite_ctx;
}

void js_sqlite_cleanup(SqliteContext *sqlite_ctx)
{
    if (sqlite_ctx)
        sqlite_destroy(sqlite_ctx);
}