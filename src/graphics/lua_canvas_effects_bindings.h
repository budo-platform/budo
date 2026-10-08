#ifndef BUDO_LUA_CANVAS_EFFECTS_BINDINGS_H
#define BUDO_LUA_CANVAS_EFFECTS_BINDINGS_H

struct lua_State;

#ifdef __cplusplus
extern "C"
{
#endif

    void lua_canvas_effects_register(struct lua_State *state, int sys_index);

#ifdef __cplusplus
}
#endif

#endif