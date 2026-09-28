#include "neural/js_neural_bindings.h"
#include "neural/lua_neural_bindings.h"
#include "tests/neural_mock.h"

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
    NeuralContext *neural;
} JsNeuralRuntime;

typedef struct
{
    lua_State *state;
    NeuralContext *neural;
} LuaNeuralRuntime;

static void js_eval_or_fail(JsNeuralRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "neural_binding_state_test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript neural isolation failure: %s\n",
                message ? message : "exception");
        if (message)
            JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_runtime_create(JsNeuralRuntime *runtime, const char *project_dir)
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
    runtime->neural = neural_create(project_dir);
    assert(runtime->neural);
    js_neural_init(runtime->context, runtime->neural);
}

static void js_runtime_destroy(JsNeuralRuntime *runtime)
{
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
    neural_destroy(runtime->neural);
}

static void js_assert_context(JsNeuralRuntime *runtime, const char *project_dir)
{
    char script[512];
    snprintf(script, sizeof(script),
             "if (!sys.neural.isAvailable()) throw Error('unavailable');\n"
             "if (sys.neural.getError() !== '%s') throw Error('wrong context');\n"
             "if (sys.neural.loadModel('model.onnx') !== 0) throw Error('load failed');\n"
             "if (sys.neural.getModelInfo(0).inputs[0].name !== 'input') throw Error('wrong model info');\n",
             project_dir);
    js_eval_or_fail(runtime, script);
}

static void test_javascript(void)
{
    JsNeuralRuntime runtime_a;
    JsNeuralRuntime runtime_b;

    js_runtime_create(&runtime_a, "js-a");
    js_assert_context(&runtime_a, "js-a");
    js_runtime_create(&runtime_b, "js-b");
    js_assert_context(&runtime_b, "js-b");
    js_assert_context(&runtime_a, "js-a");
    js_runtime_destroy(&runtime_b);
    js_assert_context(&runtime_a, "js-a");
    js_runtime_destroy(&runtime_a);
}

static void lua_eval_or_fail(LuaNeuralRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua neural isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaNeuralRuntime *runtime, const char *project_dir)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");
    runtime->neural = neural_create(project_dir);
    assert(runtime->neural);
    lua_neural_init(runtime->state, runtime->neural);
}

static void lua_runtime_destroy(LuaNeuralRuntime *runtime)
{
    lua_close(runtime->state);
    neural_destroy(runtime->neural);
}

static void lua_assert_context(LuaNeuralRuntime *runtime, const char *project_dir)
{
    char script[512];
    snprintf(script, sizeof(script),
             "assert(sys.neural.isAvailable())\n"
             "assert(sys.neural.getError() == '%s')\n"
             "assert(sys.neural.loadModel('model.onnx') == 0)\n"
             "assert(sys.neural.getModelInfo(0).inputs[1].name == 'input')\n",
             project_dir);
    lua_eval_or_fail(runtime, script);
}

static void test_lua(void)
{
    LuaNeuralRuntime runtime_a;
    LuaNeuralRuntime runtime_b;

    lua_runtime_create(&runtime_a, "lua-a");
    lua_assert_context(&runtime_a, "lua-a");
    lua_runtime_create(&runtime_b, "lua-b");
    lua_assert_context(&runtime_b, "lua-b");
    lua_assert_context(&runtime_a, "lua-a");
    lua_runtime_destroy(&runtime_b);
    lua_assert_context(&runtime_a, "lua-a");
    lua_runtime_destroy(&runtime_a);
}

int main(void)
{
    test_javascript();
    test_lua();
    puts("{\"runtime\":\"javascript\",\"operation\":\"neural.available\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"neural.loadModel\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"neural.instanceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"neural.available\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"neural.loadModel\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"neural.instanceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("managed neural binding state isolation tests passed");
    return 0;
}