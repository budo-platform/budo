#ifndef LUA_DEVICE_BINDINGS_H
#define LUA_DEVICE_BINDINGS_H

#include "device_service.h"

#ifdef __cplusplus
extern "C"
{
#endif

    DeviceContext *lua_device_init(void *L_void);

#ifdef __cplusplus
}
#endif

#endif