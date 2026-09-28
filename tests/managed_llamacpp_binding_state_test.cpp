#include "file/file_wrapper.h"
#include "llamacpp/js_llamacpp_bindings.h"
#include "llamacpp/lua_llamacpp_bindings.h"

#include <quickjs.h>
extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

#include <cassert>
#include <cstdio>
#include <cstring>

struct JsLlamaRuntime
{
    JSRuntime *runtime{};
    JSContext *context{};
    FileContext *files{};
    JsLlamaCppContext *binding{};
};

struct LuaLlamaRuntime
{
    lua_State *state{};
    FileContext *files{};
    LuaLlamaCppContext *binding{};
};

static void js_eval_or_fail(JsLlamaRuntime &runtime, const char *script)
{
    JSValue result = JS_Eval(runtime.context, script, std::strlen(script),
                             "managed_llamacpp_binding_state_test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime.context);
        const char *message = JS_ToCString(runtime.context, exception);
        std::fprintf(stderr, "JavaScript llama.cpp state test failed: %s\n",
                     message ? message : "exception");
        if (message)
            JS_FreeCString(runtime.context, message);
        JS_FreeValue(runtime.context, exception);
        assert(false);
    }
    JS_FreeValue(runtime.context, result);
}

static void js_runtime_create(JsLlamaRuntime &runtime, const char *root)
{
    runtime.runtime = JS_NewRuntime();
    assert(runtime.runtime);
    runtime.context = JS_NewContext(runtime.runtime);
    assert(runtime.context);
    JSValue global = JS_GetGlobalObject(runtime.context);
    assert(JS_SetPropertyStr(runtime.context, global, "sys",
                             JS_NewObject(runtime.context)) >= 0);
    JS_FreeValue(runtime.context, global);
    runtime.files = file_create(root);
    assert(runtime.files);
    runtime.binding = js_llamacpp_init(runtime.context, runtime.files);
    assert(runtime.binding);
}

static void js_runtime_destroy(JsLlamaRuntime &runtime)
{
    js_llamacpp_cleanup(runtime.binding);
    file_destroy(runtime.files);
    JS_FreeContext(runtime.context);
    JS_FreeRuntime(runtime.runtime);
    runtime = {};
}

static void js_assert_available(JsLlamaRuntime &runtime)
{
    js_eval_or_fail(runtime,
                    "{"
                    "if (!sys.llamacpp.isAvailable()) throw Error('unavailable');"
                    "const devices = sys.llamacpp.getDevices();"
                    "if (!Array.isArray(devices) || devices.length < 1) throw Error('devices');"
                    "for (const device of devices) {"
                    "  if (typeof device.id !== 'string' || typeof device.backend !== 'string' ||"
                    "      typeof device.name !== 'string' || typeof device.available !== 'boolean' ||"
                    "      typeof device.reason !== 'string') throw Error('device shape');"
                    "}"
                    "if (sys.llamacpp.getError() !== '') throw Error('unexpected error');"
                    "}");
}

static void lua_eval_or_fail(LuaLlamaRuntime &runtime, const char *script)
{
    if (luaL_dostring(runtime.state, script) != LUA_OK)
    {
        std::fprintf(stderr, "Lua llama.cpp state test failed: %s\n",
                     lua_tostring(runtime.state, -1));
        assert(false);
    }
}

static void lua_runtime_create(LuaLlamaRuntime &runtime, const char *root)
{
    runtime.state = luaL_newstate();
    assert(runtime.state);
    luaL_openlibs(runtime.state);
    lua_newtable(runtime.state);
    lua_setglobal(runtime.state, "sys");
    runtime.files = file_create(root);
    assert(runtime.files);
    runtime.binding = lua_llamacpp_init(runtime.state, runtime.files);
    assert(runtime.binding);
}

static void lua_runtime_destroy(LuaLlamaRuntime &runtime)
{
    lua_llamacpp_cleanup(runtime.binding);
    file_destroy(runtime.files);
    lua_close(runtime.state);
    runtime = {};
}

static void lua_assert_available(LuaLlamaRuntime &runtime)
{
    lua_eval_or_fail(runtime,
                     "assert(sys.llamacpp.isAvailable())\n"
                     "local devices = sys.llamacpp.getDevices()\n"
                     "assert(type(devices) == 'table' and #devices >= 1)\n"
                     "for _, device in ipairs(devices) do\n"
                     "  assert(type(device.id) == 'string')\n"
                     "  assert(type(device.backend) == 'string')\n"
                     "  assert(type(device.name) == 'string')\n"
                     "  assert(type(device.available) == 'boolean')\n"
                     "  assert(type(device.reason) == 'string')\n"
                     "end\n"
                     "assert(sys.llamacpp.getError() == '')\n");
}

int main()
{
    JsLlamaRuntime js_a, js_b;
    js_runtime_create(js_a, ".");
    js_runtime_create(js_b, ".");
    js_assert_available(js_a);
    js_assert_available(js_b);
    js_runtime_destroy(js_b);
    js_assert_available(js_a);
    js_runtime_destroy(js_a);

    LuaLlamaRuntime lua_a, lua_b;
    lua_runtime_create(lua_a, ".");
    lua_runtime_create(lua_b, ".");
    lua_assert_available(lua_a);
    lua_assert_available(lua_b);
    lua_runtime_destroy(lua_b);
    lua_assert_available(lua_a);
    lua_runtime_destroy(lua_a);

    std::puts("managed llama.cpp binding state isolation tests passed");
    return 0;
}
