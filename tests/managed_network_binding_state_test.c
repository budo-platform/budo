#include "network/js_network_bindings.h"
#include "network/lua_network_bindings.h"
#include "tests/network_mock.h"

#include "lauxlib.h"
#include "lualib.h"
#include "quickjs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct JsNetworkRuntime
{
    JSRuntime *runtime;
    JSContext *context;
    JsNetworkContext *network_state;
    NetworkContext *network;
} JsNetworkRuntime;

typedef struct LuaNetworkRuntime
{
    lua_State *state;
    LuaNetworkContext *network_state;
    NetworkContext *network;
} LuaNetworkRuntime;

static void js_eval_or_fail(JsNetworkRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "network_binding_state_test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript network isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_drain_jobs(JsNetworkRuntime *runtime)
{
    JSContext *job_context = NULL;
    int result;

    do
    {
        result = JS_ExecutePendingJob(runtime->runtime, &job_context);
        if (result < 0)
        {
            JSContext *context = job_context ? job_context : runtime->context;
            JSValue exception = JS_GetException(context);
            const char *message = JS_ToCString(context, exception);
            fprintf(stderr, "JavaScript Promise job failure: %s\n",
                    message ? message : "exception");
            JS_FreeCString(context, message);
            JS_FreeValue(context, exception);
            assert(0);
        }
    } while (result > 0);
}

static void js_runtime_create(JsNetworkRuntime *runtime)
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
    runtime->network_state = js_network_init(runtime->context, NULL);
    assert(runtime->network_state);
    runtime->network = js_network_context(runtime->network_state);
    assert(runtime->network);
    js_eval_or_fail(runtime,
                    "var fetchCount = 0, fetchValue = '', fetchError = '';\n"
                    "function startFetch(url) {\n"
                    "  sys.net.fetch(url).then(function (response) {\n"
                    "    fetchCount++;\n"
                    "    fetchValue = response.status + ':' + response.headers.get('x-mock') + ':' + response.text();\n"
                    "  }, function (error) { fetchError = String(error); });\n"
                    "}");
}

static void js_runtime_destroy(JsNetworkRuntime *runtime)
{
    js_network_cleanup(runtime->network_state);
    js_eval_or_fail(runtime,
                    "(function () { var threw = false; try { sys.net.fetch('https://stale.test'); } catch (error) { threw = true; } if (!threw) throw Error('fetch remained active after cleanup'); })();");
    js_network_cleanup(runtime->network_state);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
    memset(runtime, 0, sizeof(*runtime));
}

static void js_assert_result(JsNetworkRuntime *runtime, int count,
                             const char *value)
{
    char script[768];
    snprintf(script, sizeof(script),
             "if (fetchCount !== %d || fetchValue !== '%s' || fetchError !== '') throw Error('wrong fetch state: ' + fetchCount + '/' + fetchValue + '/' + fetchError);",
             count, value);
    js_eval_or_fail(runtime, script);
}

static void test_javascript(void)
{
    JsNetworkRuntime runtime_a;
    JsNetworkRuntime runtime_b;
    int request_a;
    int request_b;
    int request_after_destroy;

    network_mock_reset();
    js_runtime_create(&runtime_a);
    js_runtime_create(&runtime_b);
    js_eval_or_fail(&runtime_a, "startFetch('https://js-a.test/first');");
    js_eval_or_fail(&runtime_b, "startFetch('https://js-b.test/stale');");
    request_a = network_mock_find_request(runtime_a.network,
                                          "https://js-a.test/first");
    request_b = network_mock_find_request(runtime_b.network,
                                          "https://js-b.test/stale");
    assert(request_a > 0 && request_b > 0 && request_a != request_b);

    js_runtime_destroy(&runtime_b);
    assert(network_mock_live_count() == 1);
    assert(network_mock_complete(request_b, 200, "stale-js"));
    assert(network_mock_discarded_completion_count() == 1);

    assert(network_mock_complete(request_a, 201, "js-a"));
    js_network_poll(runtime_a.network_state);
    js_drain_jobs(&runtime_a);
    js_assert_result(&runtime_a, 1, "201:js-a:js-a");

    js_eval_or_fail(&runtime_a, "startFetch('https://js-a.test/second');");
    request_after_destroy = network_mock_find_request(
        runtime_a.network, "https://js-a.test/second");
    assert(request_after_destroy > 0);
    assert(network_mock_complete(request_after_destroy, 202, "js-again"));
    js_network_poll(runtime_a.network_state);
    js_drain_jobs(&runtime_a);
    js_assert_result(&runtime_a, 2, "202:js-again:js-again");

    js_runtime_destroy(&runtime_a);
    assert(network_mock_live_count() == 0);
    assert(network_mock_destroy_count() == 2);
}

static void lua_eval_or_fail(LuaNetworkRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua network isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaNetworkRuntime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");
    runtime->network_state = lua_network_init(runtime->state, NULL);
    assert(runtime->network_state);
    runtime->network = lua_network_context(runtime->network_state);
    assert(runtime->network);
    lua_eval_or_fail(runtime,
                     "fetch_count, fetch_value, fetch_error = 0, '', ''\n"
                     "function start_fetch(url)\n"
                     "  sys.net.fetch(url, function(response, err)\n"
                     "    if err then fetch_error = err; return end\n"
                     "    fetch_count = fetch_count + 1\n"
                     "    fetch_value = response.status .. ':' .. response.headers['x-mock'] .. ':' .. response.body\n"
                     "  end)\n"
                     "end");
}

static void lua_runtime_destroy(LuaNetworkRuntime *runtime)
{
    lua_network_cleanup(runtime->network_state);
    lua_eval_or_fail(runtime,
                     "local ok = pcall(fetch, 'https://stale.test', function() end)\nassert(not ok)");
    lua_network_cleanup(runtime->network_state);
    lua_close(runtime->state);
    memset(runtime, 0, sizeof(*runtime));
}

static void lua_assert_result(LuaNetworkRuntime *runtime, int count,
                              const char *value)
{
    char script[768];
    snprintf(script, sizeof(script),
             "assert(fetch_count == %d and fetch_value == '%s' and fetch_error == '')",
             count, value);
    lua_eval_or_fail(runtime, script);
}

static void test_lua(void)
{
    LuaNetworkRuntime runtime_a;
    LuaNetworkRuntime runtime_b;
    int request_a;
    int request_b;
    int request_after_destroy;

    network_mock_reset();
    lua_runtime_create(&runtime_a);
    lua_runtime_create(&runtime_b);
    lua_eval_or_fail(&runtime_a, "start_fetch('https://lua-a.test/first')");
    lua_eval_or_fail(&runtime_b, "start_fetch('https://lua-b.test/stale')");
    request_a = network_mock_find_request(runtime_a.network,
                                          "https://lua-a.test/first");
    request_b = network_mock_find_request(runtime_b.network,
                                          "https://lua-b.test/stale");
    assert(request_a > 0 && request_b > 0 && request_a != request_b);

    lua_runtime_destroy(&runtime_b);
    assert(network_mock_live_count() == 1);
    assert(network_mock_complete(request_b, 200, "stale-lua"));
    assert(network_mock_discarded_completion_count() == 1);

    assert(network_mock_complete(request_a, 211, "lua-a"));
    lua_network_poll(runtime_a.network_state);
    lua_assert_result(&runtime_a, 1, "211:lua-a:lua-a");

    lua_eval_or_fail(&runtime_a, "start_fetch('https://lua-a.test/second')");
    request_after_destroy = network_mock_find_request(
        runtime_a.network, "https://lua-a.test/second");
    assert(request_after_destroy > 0);
    assert(network_mock_complete(request_after_destroy, 212, "lua-again"));
    lua_network_poll(runtime_a.network_state);
    lua_assert_result(&runtime_a, 2, "212:lua-again:lua-again");

    lua_runtime_destroy(&runtime_a);
    assert(network_mock_live_count() == 0);
    assert(network_mock_destroy_count() == 2);
}

int main(void)
{
    test_javascript();
    test_lua();
    puts("{\"runtime\":\"javascript\",\"operation\":\"network.complete\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"network.staleCompletionDiscarded\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"network.cleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"network.complete\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"network.staleCompletionDiscarded\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"network.cleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("managed network binding state isolation tests passed");
    return 0;
}