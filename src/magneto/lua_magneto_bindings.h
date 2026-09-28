#ifndef LUA_MAGNETO_BINDINGS_H
#define LUA_MAGNETO_BINDINGS_H

#include <stdbool.h>
#include "magneto_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    MagnetoContext *lua_magneto_init(void *L);

    void lua_magneto_cleanup(MagnetoContext *magneto_ctx);

#ifdef __cplusplus
}
#endif

#endif