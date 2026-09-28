#ifndef LUA_UDP_BINDINGS_H
#define LUA_UDP_BINDINGS_H

#include <stdbool.h>
#include "udp_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LuaUdpContext LuaUdpContext;

    LuaUdpContext *lua_udp_init(void *L);

    UdpContext *lua_udp_context(LuaUdpContext *state);

    void lua_udp_cleanup(LuaUdpContext *state);

    void lua_udp_poll(LuaUdpContext *state);

#ifdef __cplusplus
}
#endif

#endif