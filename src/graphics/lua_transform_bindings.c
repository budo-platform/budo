#include "lua_transform_bindings.h"

#include "lua_canvas_bindings.h"

#include "lauxlib.h"
#include "lua.h"

static const luaL_Reg transform_functions[] = {
    
    {NULL, NULL},
};

void lua_transform_register(lua_State *state, int parent_index)
{
    lua_newtable(state);
    luaL_setfuncs(state, transform_functions, 0);
    lua_setfield(state, parent_index > 0 ? parent_index : parent_index - 1,
                 "transform");
}