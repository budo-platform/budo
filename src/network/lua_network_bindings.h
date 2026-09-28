#ifndef LUA_NETWORK_BINDINGS_H
#define LUA_NETWORK_BINDINGS_H

#include <stdbool.h>
#include "network_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LuaNetworkContext LuaNetworkContext;

    LuaNetworkContext *lua_network_init(void *L, const char *project_dir);

    NetworkContext *lua_network_context(LuaNetworkContext *state);

    void lua_network_poll(LuaNetworkContext *state);

    void lua_network_cleanup(LuaNetworkContext *state);

#ifdef __cplusplus
}
#endif

#endif