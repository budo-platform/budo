#ifndef BUDO_LUA_TRANSFORM_BINDINGS_H
#define BUDO_LUA_TRANSFORM_BINDINGS_H

struct lua_State;

#ifdef __cplusplus
extern "C"
{
#endif

    void lua_transform_register(struct lua_State *state, int parent_index);

#ifdef __cplusplus
}
#endif

#endif