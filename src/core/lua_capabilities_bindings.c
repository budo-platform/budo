#include "lua_capabilities_bindings.h"
#include "capabilities.h"

#include <lua.h>
#include <lauxlib.h>

static void push_cap_entry(lua_State *L, bool available)
{
    lua_newtable(L);
    lua_pushboolean(L, available ? 1 : 0);
    lua_setfield(L, -2, "available");
}

void lua_capabilities_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    ApiError error;
    CapabilitySnapshot snapshot;

    if (!capabilities_get_snapshot(&snapshot, &error))
        return;

    lua_getglobal(L, "sys");

    lua_newtable(L); 

    push_cap_entry(L, snapshot.neural);
    lua_setfield(L, -2, "neural");

    push_cap_entry(L, snapshot.llamacpp);
    lua_setfield(L, -2, "llamacpp");

    push_cap_entry(L, snapshot.midi);
    lua_setfield(L, -2, "midi");

    push_cap_entry(L, snapshot.udp);
    lua_setfield(L, -2, "udp");

    push_cap_entry(L, snapshot.http);
    lua_setfield(L, -2, "http");

    push_cap_entry(L, snapshot.sensors);
    lua_setfield(L, -2, "sensors");

    lua_setfield(L, -2, "capabilities");
    lua_pop(L, 1); 
}