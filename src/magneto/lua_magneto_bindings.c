#include "lua_magneto_bindings.h"
#include "magneto_service.h"
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"

static MagnetoContext *lua_magneto_context(lua_State *L)
{
    return (MagnetoContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static int l_magneto_is_available(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    lua_pushboolean(L, magneto_ctx ? magneto_is_available(magneto_ctx) : 0);
    return 1;
}

static int l_magneto_has_accelerometer(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    lua_pushboolean(L, magneto_ctx ? magneto_has_accelerometer(magneto_ctx) : 0);
    return 1;
}

static int l_magneto_has_compass(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    lua_pushboolean(L, magneto_ctx ? magneto_has_compass(magneto_ctx) : 0);
    return 1;
}

static int l_magneto_start(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    ApiError error;
    lua_pushboolean(L, magneto_service_start(magneto_ctx, &error));
    return 1;
}

static int l_magneto_stop(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    if (magneto_ctx)
        magneto_stop(magneto_ctx);
    return 0;
}

static int l_magneto_is_active(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    lua_pushboolean(L, magneto_ctx ? magneto_is_active(magneto_ctx) : 0);
    return 1;
}

static int l_magneto_get_accel(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    if (!magneto_ctx)
    {
        lua_pushnil(L);
        return 1;
    }

    MagnetoAccelData data;
    if (!magneto_get_accel(magneto_ctx, &data))
    {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, data.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, data.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, data.z);
    lua_setfield(L, -2, "z");
    return 1;
}

static int l_magneto_get_compass(lua_State *L)
{
    MagnetoContext *magneto_ctx = lua_magneto_context(L);
    if (!magneto_ctx)
    {
        lua_pushnil(L);
        return 1;
    }

    MagnetoCompassData data;
    if (!magneto_get_compass(magneto_ctx, &data))
    {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, data.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, data.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, data.z);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, data.heading);
    lua_setfield(L, -2, "heading");
    return 1;
}

static const luaL_Reg magneto_funcs[] = {
    {"isAvailable", l_magneto_is_available},
    {"hasAccelerometer", l_magneto_has_accelerometer},
    {"hasCompass", l_magneto_has_compass},
    {"start", l_magneto_start},
    {"stop", l_magneto_stop},
    {"isActive", l_magneto_is_active},
    {"getAccel", l_magneto_get_accel},
    {"getCompass", l_magneto_get_compass},
    {NULL, NULL}};

MagnetoContext *lua_magneto_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;

    MagnetoContext *magneto_ctx = magneto_create();
    if (!magneto_ctx)
    {
        fprintf(stderr, "Failed to create magneto context\n");
        return NULL;
    }

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, magneto_ctx);
    luaL_setfuncs(L, magneto_funcs, 1);
    lua_setfield(L, -2, "magneto");

    lua_pop(L, 1); 

    return magneto_ctx;
}

void lua_magneto_cleanup(MagnetoContext *magneto_ctx)
{
    if (magneto_ctx)
        magneto_destroy(magneto_ctx);
}