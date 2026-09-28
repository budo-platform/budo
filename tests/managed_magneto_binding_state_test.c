#include "magneto/js_magneto_bindings.h"
#include "magneto/lua_magneto_bindings.h"
#include "tests/magneto_mock.h"

#include "lauxlib.h"
#include "lualib.h"
#include "quickjs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    JSRuntime *runtime;
    JSContext *context;
    MagnetoContext *magneto;
} JsMagnetoRuntime;

typedef struct
{
    lua_State *state;
    MagnetoContext *magneto;
} LuaMagnetoRuntime;

static void js_eval_or_fail(JsMagnetoRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "magneto_binding_state_test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript magneto isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_runtime_create(JsMagnetoRuntime *runtime, float value)
{
    JSValue global;

    memset(runtime, 0, sizeof(*runtime));
    runtime->runtime = JS_NewRuntime();
    assert(runtime->runtime);
    runtime->context = JS_NewContext(runtime->runtime);
    assert(runtime->context);
    global = JS_GetGlobalObject(runtime->context);
    assert(JS_SetPropertyStr(runtime->context, global, "sys",
                             JS_NewObject(runtime->context)) >= 0);
    JS_FreeValue(runtime->context, global);
    runtime->magneto = js_magneto_init(runtime->context);
    assert(runtime->magneto);
    magneto_mock_set_value(runtime->magneto, value);
}

static void js_runtime_destroy(JsMagnetoRuntime *runtime)
{
    js_magneto_cleanup(runtime->magneto);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
}

static void js_assert_value(JsMagnetoRuntime *runtime, int expected)
{
    char script[1024];
    snprintf(script, sizeof(script),
             "(function () {\n"
             "  if (!sys.sensors.isAvailable() || !sys.sensors.hasAccelerometer() || !sys.sensors.hasCompass()) throw Error('unavailable');\n"
             "  if (!sys.sensors.start() || !sys.sensors.isActive()) throw Error('start failed');\n"
             "  const accel = sys.sensors.getAccel();\n"
             "  if (!accel || accel.x !== %d || accel.y !== %d || accel.z !== %d) throw Error('wrong accel');\n"
             "  const compass = sys.sensors.getCompass();\n"
             "  if (!compass || compass.x !== %d || compass.y !== %d || compass.z !== %d || compass.heading !== %d) throw Error('wrong compass');\n"
             "})();\n",
             expected, expected + 1, expected + 2,
             expected + 3, expected + 4, expected + 5, expected + 6);
    js_eval_or_fail(runtime, script);
}

static void test_javascript(void)
{
    JsMagnetoRuntime runtime_a;
    JsMagnetoRuntime runtime_b;

    js_runtime_create(&runtime_a, 11.0f);
    js_assert_value(&runtime_a, 11);
    js_runtime_create(&runtime_b, 22.0f);
    js_assert_value(&runtime_b, 22);
    js_assert_value(&runtime_a, 11);
    js_runtime_destroy(&runtime_b);
    js_assert_value(&runtime_a, 11);
    js_eval_or_fail(&runtime_a,
                    "sys.sensors.stop(); if (sys.sensors.isActive() || sys.sensors.getAccel() !== null) throw Error('stop failed');");
    js_runtime_destroy(&runtime_a);
}

static void lua_eval_or_fail(LuaMagnetoRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua magneto isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaMagnetoRuntime *runtime, float value)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");
    runtime->magneto = lua_magneto_init(runtime->state);
    assert(runtime->magneto);
    magneto_mock_set_value(runtime->magneto, value);
}

static void lua_runtime_destroy(LuaMagnetoRuntime *runtime)
{
    lua_magneto_cleanup(runtime->magneto);
    lua_close(runtime->state);
}

static void lua_assert_value(LuaMagnetoRuntime *runtime, int expected)
{
    char script[1024];
    snprintf(script, sizeof(script),
             "assert(sys.sensors.isAvailable() and sys.sensors.hasAccelerometer() and sys.sensors.hasCompass())\n"
             "assert(sys.sensors.start() and sys.sensors.isActive())\n"
             "local accel = sys.sensors.getAccel()\n"
             "assert(accel and accel.x == %d and accel.y == %d and accel.z == %d)\n"
             "local compass = sys.sensors.getCompass()\n"
             "assert(compass and compass.x == %d and compass.y == %d and compass.z == %d and compass.heading == %d)\n",
             expected, expected + 1, expected + 2,
             expected + 3, expected + 4, expected + 5, expected + 6);
    lua_eval_or_fail(runtime, script);
}

static void test_lua(void)
{
    LuaMagnetoRuntime runtime_a;
    LuaMagnetoRuntime runtime_b;

    lua_runtime_create(&runtime_a, 33.0f);
    lua_assert_value(&runtime_a, 33);
    lua_runtime_create(&runtime_b, 44.0f);
    lua_assert_value(&runtime_b, 44);
    lua_assert_value(&runtime_a, 33);
    lua_runtime_destroy(&runtime_b);
    lua_assert_value(&runtime_a, 33);
    lua_eval_or_fail(&runtime_a,
                     "sys.sensors.stop()\nassert(not sys.sensors.isActive() and sys.sensors.getAccel() == nil)");
    lua_runtime_destroy(&runtime_a);
}

int main(void)
{
    test_javascript();
    test_lua();
    puts("managed magneto binding state isolation tests passed");
    return 0;
}