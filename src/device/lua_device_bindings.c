#include "lua_device_bindings.h"
#include "device_service.h"

#include "lua.h"
#include "lauxlib.h"

static int l_device_keep_screen_on(lua_State *L)
{
    ApiError error;
    DeviceContext *state =
        (DeviceContext *)lua_touserdata(L, lua_upvalueindex(1));
    bool enabled = false;
    if (lua_gettop(L) >= 1)
        enabled = lua_toboolean(L, 1) ? true : false;
    bool ok = device_binding_state_keep_screen_on(state, enabled, &error);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

DeviceContext *lua_device_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    DeviceContext *state = device_binding_state_create();
    if (!state)
        return NULL;

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, state);
    lua_pushcclosure(L, l_device_keep_screen_on, 1);
    lua_setfield(L, -2, "keepScreenOn");
    lua_setfield(L, -2, "device");

    lua_pop(L, 1); 
    return state;
}