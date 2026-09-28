#include "lua_file_bindings.h"
#include "file_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

static FileContext *file_context(lua_State *L)
{
    return (FileContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static bool asset_path(const char *path, char output[FILE_MAX_PATH])
{
    int length = path[0]
                     ? snprintf(output, FILE_MAX_PATH, "assets/%s", path)
                     : snprintf(output, FILE_MAX_PATH, "assets");
    return length >= 0 && length < FILE_MAX_PATH;
}

static const char *check_path(lua_State *L, const char *method, int expected_argc,
                              bool allow_empty)
{
    luaL_argcheck(L, lua_gettop(L) == expected_argc, 1, "wrong number of arguments");
    luaL_argcheck(L, lua_type(L, 1) == LUA_TSTRING, 1, "path must be a string");
    const char *path = lua_tostring(L, 1);
    bool writable = strcmp(method, "writeText") == 0 ||
                    strcmp(method, "writeBinary") == 0;
    if (!file_virtual_path_is_valid(path, allow_empty, writable))
        luaL_error(L, "files.%s requires an assets/ or files/ path%s",
                   method, writable ? " under files/" : "");
    return path;
}

static int l_file_list(lua_State *L)
{
    const char *rel_path = "";
    int argc = lua_gettop(L);
    luaL_argcheck(L, argc <= 1, 1, "expected zero or one argument");
    if (argc == 1)
        rel_path = check_path(L, "list", 1, true);

    FileListResult *result = file_list(file_context(L), rel_path);

    if (!result)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, result->count, 0);

    for (int i = 0; i < result->count; i++)
    {
        lua_newtable(L);
        lua_pushstring(L, result->entries[i].name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, result->entries[i].type == FILE_ENTRY_DIRECTORY ? "directory" : "file");
        lua_setfield(L, -2, "type");
        if (result->entries[i].type == FILE_ENTRY_FILE && result->entries[i].size >= 0)
        {
            lua_pushinteger(L, result->entries[i].size);
            lua_setfield(L, -2, "size");
        }
        lua_rawseti(L, -2, i + 1);
    }

    file_list_free(result);
    return 1;
}

static int l_asset_list(lua_State *L)
{
    const char *path = "";
    int argc = lua_gettop(L);
    luaL_argcheck(L, argc <= 1, 1, "expected zero or one argument");
    if (argc == 1)
    {
        luaL_argcheck(L, lua_type(L, 1) == LUA_TSTRING, 1, "path must be a string");
        path = lua_tostring(L, 1);
        luaL_argcheck(L, file_path_is_valid(path, true), 1, "path must be safe and relative");
    }
    char virtual_path[FILE_MAX_PATH];
    luaL_argcheck(L, asset_path(path, virtual_path), 1, "path is too long");
    FileListResult *result = file_list(file_context(L), virtual_path);
    if (!result)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, result->count, 0);
    for (int i = 0; i < result->count; i++)
    {
        lua_newtable(L);
        lua_pushstring(L, result->entries[i].name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, result->entries[i].type == FILE_ENTRY_DIRECTORY ? "directory" : "file");
        lua_setfield(L, -2, "type");
        if (result->entries[i].type == FILE_ENTRY_FILE && result->entries[i].size >= 0)
        {
            lua_pushinteger(L, result->entries[i].size);
            lua_setfield(L, -2, "size");
        }
        lua_rawseti(L, -2, i + 1);
    }
    file_list_free(result);
    return 1;
}

static void check_asset_path(lua_State *L, const char *method,
                             char output[FILE_MAX_PATH])
{
    luaL_argcheck(L, lua_gettop(L) == 1, 1, "wrong number of arguments");
    luaL_argcheck(L, lua_type(L, 1) == LUA_TSTRING, 1, "path must be a string");
    const char *path = lua_tostring(L, 1);
    if (!file_path_is_valid(path, false) || !asset_path(path, output))
        luaL_error(L, "assets.%s requires a safe relative path", method);
}

static int l_asset_read_text(lua_State *L)
{
    char path[FILE_MAX_PATH];
    check_asset_path(L, "readText", path);
    size_t length = 0;
    ApiError error;
    char *content = file_service_read_text_virtual(file_context(L), path,
                                                   &length, &error);
    if (!content)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, content, length);
    free(content);
    return 1;
}

static int l_asset_read_binary(lua_State *L)
{
    char path[FILE_MAX_PATH];
    check_asset_path(L, "readBinary", path);
    size_t length = 0;
    uint8_t *data = file_read_binary(file_context(L), path, &length);
    if (!data)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char *)data, length);
    free(data);
    return 1;
}

static int l_asset_exists(lua_State *L)
{
    char path[FILE_MAX_PATH];
    check_asset_path(L, "exists", path);
    lua_pushboolean(L, file_is_file(file_context(L), path));
    return 1;
}

static int l_asset_is_directory(lua_State *L)
{
    char path[FILE_MAX_PATH];
    check_asset_path(L, "isDirectory", path);
    lua_pushboolean(L, file_is_directory(file_context(L), path));
    return 1;
}

static int l_asset_size(lua_State *L)
{
    char path[FILE_MAX_PATH];
    check_asset_path(L, "size", path);
    int64_t size = file_size(file_context(L), path);
    if (size < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, size);
    return 1;
}

static int l_file_read_text(lua_State *L)
{
    const char *rel_path = check_path(L, "readText", 1, false);

    size_t len = 0;
    ApiError error;
    char *content = file_service_read_text_virtual(file_context(L), rel_path,
                                                   &len, &error);

    if (!content)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlstring(L, content, len);
    free(content);
    return 1;
}

static int l_file_read_binary(lua_State *L)
{
    const char *rel_path = check_path(L, "readBinary", 1, false);

    size_t len = 0;
    uint8_t *data = file_read_binary(file_context(L), rel_path, &len);

    if (!data)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlstring(L, (const char *)data, len);
    free(data);
    return 1;
}

static int l_file_exists(lua_State *L)
{
    const char *rel_path = check_path(L, "exists", 1, false);
    lua_pushboolean(L, file_is_file(file_context(L), rel_path));
    return 1;
}

static int l_file_is_directory(lua_State *L)
{
    const char *rel_path = check_path(L, "isDirectory", 1, false);
    lua_pushboolean(L, file_is_directory(file_context(L), rel_path));
    return 1;
}

static int l_file_size(lua_State *L)
{
    const char *rel_path = check_path(L, "size", 1, false);
    int64_t sz = file_size(file_context(L), rel_path);

    if (sz < 0)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, sz);
    return 1;
}

static int l_file_write_text(lua_State *L)
{
    const char *rel_path = check_path(L, "writeText", 2, false);
    luaL_argcheck(L, lua_type(L, 2) == LUA_TSTRING, 2, "text must be a string");
    size_t len = 0;
    const char *text = lua_tolstring(L, 2, &len);
    FileContext *ctx = file_context(L);
    if (!file_write_text(ctx, rel_path, text, len))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, file_get_last_write_path(ctx));
    return 1;
}

static int l_file_write_binary(lua_State *L)
{
    const char *rel_path = check_path(L, "writeBinary", 2, false);
    luaL_argcheck(L, lua_type(L, 2) == LUA_TSTRING, 2, "data must be a string");
    size_t len = 0;
    const char *data = lua_tolstring(L, 2, &len);
    FileContext *ctx = file_context(L);
    if (!file_write_binary(ctx, rel_path, (const uint8_t *)data, len))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, file_get_last_write_path(ctx));
    return 1;
}

static int l_file_get_error(lua_State *L)
{
    luaL_argcheck(L, lua_gettop(L) == 0, 1, "expected no arguments");
    FileContext *ctx = file_context(L);
    if (!ctx)
    {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, file_get_error(ctx));
    return 1;
}

static const luaL_Reg file_funcs[] = {
    {"list", l_file_list},
    {"readText", l_file_read_text},
    {"readBinary", l_file_read_binary},
    {"writeText", l_file_write_text},
    {"writeBinary", l_file_write_binary},
    {"exists", l_file_exists},
    {"isDirectory", l_file_is_directory},
    {"size", l_file_size},
    {"getError", l_file_get_error},
    {NULL, NULL}};

static const luaL_Reg asset_funcs[] = {
    {"list", l_asset_list},
    {"readText", l_asset_read_text},
    {"readBinary", l_asset_read_binary},
    {"exists", l_asset_exists},
    {"isDirectory", l_asset_is_directory},
    {"size", l_asset_size},
    {"getError", l_file_get_error},
    {NULL, NULL}};

FileContext *lua_file_init(void *L_void, const char *root_dir)
{
    lua_State *L = (lua_State *)L_void;

    FileContext *file_ctx = file_create(root_dir);
    if (!file_ctx)
    {
        fprintf(stderr, "Failed to create file context\n");
        return NULL;
    }

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, file_ctx);
    luaL_setfuncs(L, file_funcs, 1);
    lua_setfield(L, -2, "files");

    lua_newtable(L);
    lua_pushlightuserdata(L, file_ctx);
    luaL_setfuncs(L, asset_funcs, 1);
    lua_setfield(L, -2, "assets");

    lua_pop(L, 1); 

    return file_ctx;
}

void lua_file_cleanup(FileContext *file_ctx)
{
    if (file_ctx)
        file_destroy(file_ctx);
}