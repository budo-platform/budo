#include "lua_sqlite_bindings.h"
#include "sqlite_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"

static SqliteContext *lua_db_context(lua_State *L)
{
    return (SqliteContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static int l_db_open(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    const char *name = luaL_checkstring(L, 1);
    ApiError error;
    int handle = sqlite_service_open(sqlite_ctx, name, &error);

    if (handle < 0)
        return luaL_error(L, "%s", sqlite_get_error(sqlite_ctx));

    lua_pushinteger(L, handle);
    return 1;
}

static int l_db_close(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
        return 0;

    int handle = (int)luaL_checkinteger(L, 1);
    sqlite_close(sqlite_ctx, handle);
    return 0;
}

static int l_db_execute(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    const char *sql = luaL_checkstring(L, 2);

    if (!sqlite_execute(sqlite_ctx, handle, sql))
        return luaL_error(L, "%s", sqlite_get_error(sqlite_ctx));

    lua_pushboolean(L, 1);
    return 1;
}

static int extract_lua_params(lua_State *L, int start_idx, int argc,
                              SqliteParam *params, int max_params)
{
    int count = 0;
    for (int i = start_idx; i <= argc && count < max_params; i++)
    {
        int t = lua_type(L, i);
        switch (t)
        {
        case LUA_TNIL:
            params[count].type = SQLITE_VAL_NULL;
            break;
        case LUA_TBOOLEAN:
            params[count].type = SQLITE_VAL_INTEGER;
            params[count].value.integer = lua_toboolean(L, i) ? 1 : 0;
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, i))
            {
                params[count].type = SQLITE_VAL_INTEGER;
                params[count].value.integer = lua_tointeger(L, i);
            }
            else
            {
                params[count].type = SQLITE_VAL_FLOAT;
                params[count].value.real = lua_tonumber(L, i);
            }
            break;
        case LUA_TSTRING:
        {
            size_t len;
            const char *str = lua_tolstring(L, i, &len);
            params[count].type = SQLITE_VAL_TEXT;
            params[count].value.text.data = str;
            params[count].value.text.length = (int)len;
            break;
        }
        default:
            params[count].type = SQLITE_VAL_NULL;
            break;
        }
        count++;
    }
    return count;
}

static int l_db_run(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    const char *sql = luaL_checkstring(L, 2);
    int argc = lua_gettop(L);

    SqliteParam params[SQLITE_MAX_PARAMS];
    int param_count = extract_lua_params(L, 3, argc, params, SQLITE_MAX_PARAMS);

    int changes = sqlite_run(sqlite_ctx, handle, sql, params, param_count);

    if (changes < 0)
        return luaL_error(L, "%s", sqlite_get_error(sqlite_ctx));

    lua_pushinteger(L, changes);
    return 1;
}

static void push_result(lua_State *L, SqliteResult *result)
{
    if (!result)
    {
        lua_newtable(L);
        return;
    }

    if (result->error)
    {
        char err[512];
        snprintf(err, sizeof(err), "%s", result->error);
        sqlite_result_free(result);
        luaL_error(L, "%s", err);
        return;
    }

    lua_createtable(L, result->row_count, 0);

    for (int r = 0; r < result->row_count; r++)
    {
        lua_newtable(L);
        SqliteRow *row = &result->rows[r];

        for (int c = 0; c < row->column_count; c++)
        {
            switch (row->values[c].type)
            {
            case SQLITE_VAL_INTEGER:
                lua_pushinteger(L, row->values[c].value.integer);
                break;
            case SQLITE_VAL_FLOAT:
                lua_pushnumber(L, row->values[c].value.real);
                break;
            case SQLITE_VAL_TEXT:
                lua_pushlstring(L, row->values[c].value.text.data,
                                row->values[c].value.text.length);
                break;
            default:
                lua_pushnil(L);
                break;
            }
            lua_setfield(L, -2, row->column_names[c]);
        }

        lua_rawseti(L, -2, r + 1);
    }

    sqlite_result_free(result);
}

static int l_db_query(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
    {
        lua_newtable(L);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    const char *sql = luaL_checkstring(L, 2);
    int argc = lua_gettop(L);

    SqliteParam params[SQLITE_MAX_PARAMS];
    int param_count = extract_lua_params(L, 3, argc, params, SQLITE_MAX_PARAMS);

    SqliteResult *result = sqlite_query(sqlite_ctx, handle, sql, params, param_count);

    push_result(L, result);
    return 1;
}

static int l_db_last_insert_id(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
    {
        lua_pushinteger(L, 0);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, sqlite_last_insert_id(sqlite_ctx, handle));
    return 1;
}

static int l_db_get_error(lua_State *L)
{
    SqliteContext *sqlite_ctx = lua_db_context(L);
    if (!sqlite_ctx)
    {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, sqlite_get_error(sqlite_ctx));
    return 1;
}

static const luaL_Reg db_funcs[] = {
    {"open", l_db_open},
    {"close", l_db_close},
    {"execute", l_db_execute},
    {"run", l_db_run},
    {"query", l_db_query},
    {"lastInsertId", l_db_last_insert_id},
    {"getError", l_db_get_error},
    {NULL, NULL}};

SqliteContext *lua_sqlite_init(void *L_void, const char *project_dir)
{
    lua_State *L = (lua_State *)L_void;

    SqliteContext *sqlite_ctx = sqlite_create(project_dir);
    if (!sqlite_ctx)
    {
        fprintf(stderr, "Failed to create SQLite context\n");
        return NULL;
    }

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, sqlite_ctx);
    luaL_setfuncs(L, db_funcs, 1);
    lua_setfield(L, -2, "db");

    lua_pop(L, 1); 

    return sqlite_ctx;
}

void lua_sqlite_cleanup(SqliteContext *sqlite_ctx)
{
    if (sqlite_ctx)
        sqlite_destroy(sqlite_ctx);
}