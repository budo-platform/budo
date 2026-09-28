#include "managed_basics.h"

#include "core/js_capabilities_bindings.h"
#include "core/lua_capabilities_bindings.h"
#include "device/js_device_bindings.h"
#include "device/lua_device_bindings.h"
#include "math/js_math_bindings.h"
#include "math/lua_math_bindings.h"

DeviceContext *managed_basics_init_js(JSContext *context)
{
    DeviceContext *device_ctx = js_device_init(context);
    js_math_init(context);
    js_capabilities_init(context);
    return device_ctx;
}

DeviceContext *managed_basics_init_lua(void *lua_state)
{
    DeviceContext *device_ctx = lua_device_init(lua_state);
    lua_math_init(lua_state);
    lua_capabilities_init(lua_state);
    return device_ctx;
}