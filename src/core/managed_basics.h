#ifndef BUDO_MANAGED_BASICS_H
#define BUDO_MANAGED_BASICS_H

#include "device/device_service.h"
#include "quickjs.h"

#ifdef __cplusplus
extern "C"
{
#endif

    DeviceContext *managed_basics_init_js(JSContext *context);
    DeviceContext *managed_basics_init_lua(void *lua_state);

#ifdef __cplusplus
}
#endif

#endif