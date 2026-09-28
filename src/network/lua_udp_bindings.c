#include "lua_udp_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#define LUA_MAX_UDP_CALLBACKS UDP_MAX_SOCKETS

struct LuaUdpContext
{
    UdpContext *udp_ctx;
    lua_State *lua_state;
    int callbacks[LUA_MAX_UDP_CALLBACKS];
    bool callbacks_active[LUA_MAX_UDP_CALLBACKS];
};

static LuaUdpContext *lua_udp_ctx(lua_State *L)
{
    return (LuaUdpContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static int l_udp_bind(lua_State *L)
{
    LuaUdpContext *state = lua_udp_ctx(L);
    if (!state || !state->udp_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    int port = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, udp_bind(state->udp_ctx, port));
    return 1;
}

static int l_udp_get_port(lua_State *L)
{
    LuaUdpContext *state = lua_udp_ctx(L);
    if (!state || !state->udp_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, udp_get_port(state->udp_ctx, handle));
    return 1;
}

static int l_udp_send(lua_State *L)
{
    LuaUdpContext *state = lua_udp_ctx(L);
    if (!state || !state->udp_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    const char *host = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);

    int len = (int)lua_rawlen(L, 4);
    if (len <= 0 || len > UDP_MAX_PACKET)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    for (int i = 0; i < len; i++)
    {
        lua_rawgeti(L, 4, i + 1);
        buf[i] = (uint8_t)(lua_tointeger(L, -1) & 0xFF);
        lua_pop(L, 1);
    }

    bool result = udp_send(state->udp_ctx, handle, host, port, buf, len);
    free(buf);

    lua_pushboolean(L, result);
    return 1;
}

static int l_udp_on_message(lua_State *L)
{
    LuaUdpContext *state = lua_udp_ctx(L);
    if (!state)
        return 0;

    int handle = (int)luaL_checkinteger(L, 1);

    if (handle < 0 || handle >= LUA_MAX_UDP_CALLBACKS)
        return 0;

    if (state->callbacks_active[handle])
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->callbacks[handle]);
        state->callbacks[handle] = LUA_NOREF;
        state->callbacks_active[handle] = false;
    }

    if (lua_isfunction(L, 2))
    {
        lua_pushvalue(L, 2);
        state->callbacks[handle] = luaL_ref(L, LUA_REGISTRYINDEX);
        state->callbacks_active[handle] = true;
    }

    return 0;
}

static int l_udp_close(lua_State *L)
{
    LuaUdpContext *state = lua_udp_ctx(L);
    if (!state || !state->udp_ctx)
        return 0;

    int handle = (int)luaL_checkinteger(L, 1);

    if (handle >= 0 && handle < LUA_MAX_UDP_CALLBACKS && state->callbacks_active[handle])
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->callbacks[handle]);
        state->callbacks[handle] = LUA_NOREF;
        state->callbacks_active[handle] = false;
    }

    udp_close(state->udp_ctx, handle);
    return 0;
}

static const luaL_Reg udp_funcs[] = {
    {"bind", l_udp_bind},
    {"getPort", l_udp_get_port},
    {"send", l_udp_send},
    {"onMessage", l_udp_on_message},
    {"close", l_udp_close},
    {NULL, NULL}};

LuaUdpContext *lua_udp_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    LuaUdpContext *state = (LuaUdpContext *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->lua_state = L;

    for (int i = 0; i < LUA_MAX_UDP_CALLBACKS; i++)
    {
        state->callbacks[i] = LUA_NOREF;
    }

    state->udp_ctx = udp_create();
    if (!state->udp_ctx)
    {
        fprintf(stderr, "Warning: Failed to create UDP context\n");
        free(state);
        return NULL;
    }

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, state);
    luaL_setfuncs(L, udp_funcs, 1);
    lua_setfield(L, -2, "udp");

    lua_pop(L, 1); 

    return state;
}

UdpContext *lua_udp_context(LuaUdpContext *state)
{
    return state ? state->udp_ctx : NULL;
}

void lua_udp_cleanup(LuaUdpContext *state)
{
    if (!state)
        return;

    if (state->lua_state)
    {
        for (int i = 0; i < LUA_MAX_UDP_CALLBACKS; i++)
        {
            if (state->callbacks_active[i])
            {
                luaL_unref(state->lua_state, LUA_REGISTRYINDEX, state->callbacks[i]);
                state->callbacks[i] = LUA_NOREF;
                state->callbacks_active[i] = false;
            }
        }
    }

    state->lua_state = NULL;
    udp_destroy(state->udp_ctx);
    state->udp_ctx = NULL;
    free(state);
}

void lua_udp_poll(LuaUdpContext *state)
{
    if (!state || !state->lua_state || !state->udp_ctx)
        return;

    uint8_t buf[UDP_MAX_PACKET];
    UdpDatagram dgram;

    for (int handle = 0; handle < LUA_MAX_UDP_CALLBACKS; handle++)
    {
        if (!state->callbacks_active[handle])
            continue;

        for (int n = 0; n < 64; n++)
        {
            int received = udp_recv(state->udp_ctx, handle, buf, sizeof(buf), &dgram);
            if (received <= 0)
                break;

            lua_rawgeti(state->lua_state, LUA_REGISTRYINDEX, state->callbacks[handle]);

            lua_newtable(state->lua_state);

            lua_newtable(state->lua_state);
            for (int i = 0; i < received; i++)
            {
                lua_pushinteger(state->lua_state, buf[i]);
                lua_rawseti(state->lua_state, -2, i + 1);
            }
            lua_setfield(state->lua_state, -2, "data");

            lua_pushstring(state->lua_state, dgram.host);
            lua_setfield(state->lua_state, -2, "host");
            lua_pushinteger(state->lua_state, dgram.port);
            lua_setfield(state->lua_state, -2, "port");

            if (lua_pcall(state->lua_state, 1, 0, 0) != LUA_OK)
            {
                fprintf(stderr, "Lua UDP callback error: %s\n", lua_tostring(state->lua_state, -1));
                lua_pop(state->lua_state, 1);
            }
        }
    }
}