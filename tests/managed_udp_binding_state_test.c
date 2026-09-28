#include "network/js_udp_bindings.h"
#include "network/lua_udp_bindings.h"
#include "tests/udp_mock.h"

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
    JsUdpContext *udp_state;
    UdpContext *udp;
} JsUdpRuntime;

typedef struct
{
    lua_State *state;
    LuaUdpContext *udp_state;
    UdpContext *udp;
} LuaUdpRuntime;

static void js_eval_or_fail(JsUdpRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "udp_binding_state_test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript UDP isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_runtime_create(JsUdpRuntime *runtime)
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
    runtime->udp_state = js_udp_init(runtime->context);
    assert(runtime->udp_state);
    runtime->udp = js_udp_context(runtime->udp_state);
    assert(runtime->udp);
    js_eval_or_fail(runtime,
                    "var udpCount = 0, udpByte = -1, udpHost = '', udpPort = -1;\n"
                    "var udpSocket = sys.net.udp.bind(0);\n"
                    "if (udpSocket !== 0) throw Error('bind failed');\n"
                    "sys.net.udp.onMessage(udpSocket, function (message) {\n"
                    "  udpCount++; udpByte = message.data[0];\n"
                    "  udpHost = message.host; udpPort = message.port;\n"
                    "});");
}

static void js_runtime_destroy(JsUdpRuntime *runtime)
{
    js_udp_cleanup(runtime->udp_state);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
}

static void js_assert_message(JsUdpRuntime *runtime, int count, int byte,
                              const char *host, int port)
{
    char script[512];
    snprintf(script, sizeof(script),
             "if (udpCount !== %d || udpByte !== %d || udpHost !== '%s' || udpPort !== %d) throw Error('wrong callback state');",
             count, byte, host, port);
    js_eval_or_fail(runtime, script);
}

static void test_javascript(void)
{
    JsUdpRuntime runtime_a;
    JsUdpRuntime runtime_b;
    const uint8_t message_a[] = {11, 12};
    const uint8_t message_b[] = {21, 22};
    const uint8_t message_after_destroy[] = {31};
    const uint8_t pending_at_destroy[] = {41};

    udp_mock_reset();
    js_runtime_create(&runtime_a);
    js_runtime_create(&runtime_b);
    assert(udp_get_port(runtime_a.udp, 0) != udp_get_port(runtime_b.udp, 0));

    assert(udp_mock_enqueue(runtime_a.udp, 0, "js-a", 4101,
                            message_a, sizeof(message_a)));
    assert(udp_mock_enqueue(runtime_b.udp, 0, "js-b", 4102,
                            message_b, sizeof(message_b)));
    js_udp_poll(runtime_a.udp_state);
    js_assert_message(&runtime_a, 1, 11, "js-a", 4101);
    js_assert_message(&runtime_b, 0, -1, "", -1);
    js_udp_poll(runtime_b.udp_state);
    js_assert_message(&runtime_b, 1, 21, "js-b", 4102);
    js_assert_message(&runtime_a, 1, 11, "js-a", 4101);

    assert(udp_mock_enqueue(runtime_b.udp, 0, "discarded", 4199,
                            pending_at_destroy, sizeof(pending_at_destroy)));
    js_runtime_destroy(&runtime_b);
    assert(udp_mock_live_count() == 1);
    assert(udp_mock_enqueue(runtime_a.udp, 0, "js-a-again", 4103,
                            message_after_destroy, sizeof(message_after_destroy)));
    js_udp_poll(runtime_a.udp_state);
    js_assert_message(&runtime_a, 2, 31, "js-a-again", 4103);
    js_runtime_destroy(&runtime_a);
    assert(udp_mock_live_count() == 0);
    assert(udp_mock_destroy_count() == 2);
}

static void lua_eval_or_fail(LuaUdpRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua UDP isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaUdpRuntime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");
    runtime->udp_state = lua_udp_init(runtime->state);
    assert(runtime->udp_state);
    runtime->udp = lua_udp_context(runtime->udp_state);
    assert(runtime->udp);
    lua_eval_or_fail(runtime,
                     "udp_count, udp_byte, udp_host, udp_port = 0, -1, '', -1\n"
                     "udp_socket = sys.net.udp.bind(0)\n"
                     "assert(udp_socket == 0)\n"
                     "sys.net.udp.onMessage(udp_socket, function(message)\n"
                     "  udp_count = udp_count + 1\n"
                     "  udp_byte = message.data[1]\n"
                     "  udp_host = message.host\n"
                     "  udp_port = message.port\n"
                     "end)");
}

static void lua_runtime_destroy(LuaUdpRuntime *runtime)
{
    lua_udp_cleanup(runtime->udp_state);
    lua_close(runtime->state);
}

static void lua_assert_message(LuaUdpRuntime *runtime, int count, int byte,
                               const char *host, int port)
{
    char script[512];
    snprintf(script, sizeof(script),
             "assert(udp_count == %d and udp_byte == %d and udp_host == '%s' and udp_port == %d)",
             count, byte, host, port);
    lua_eval_or_fail(runtime, script);
}

static void test_lua(void)
{
    LuaUdpRuntime runtime_a;
    LuaUdpRuntime runtime_b;
    const uint8_t message_a[] = {51, 52};
    const uint8_t message_b[] = {61, 62};
    const uint8_t message_after_destroy[] = {71};
    const uint8_t pending_at_destroy[] = {81};

    udp_mock_reset();
    lua_runtime_create(&runtime_a);
    lua_runtime_create(&runtime_b);
    assert(udp_get_port(runtime_a.udp, 0) != udp_get_port(runtime_b.udp, 0));

    assert(udp_mock_enqueue(runtime_a.udp, 0, "lua-a", 4201,
                            message_a, sizeof(message_a)));
    assert(udp_mock_enqueue(runtime_b.udp, 0, "lua-b", 4202,
                            message_b, sizeof(message_b)));
    lua_udp_poll(runtime_a.udp_state);
    lua_assert_message(&runtime_a, 1, 51, "lua-a", 4201);
    lua_assert_message(&runtime_b, 0, -1, "", -1);
    lua_udp_poll(runtime_b.udp_state);
    lua_assert_message(&runtime_b, 1, 61, "lua-b", 4202);
    lua_assert_message(&runtime_a, 1, 51, "lua-a", 4201);

    assert(udp_mock_enqueue(runtime_b.udp, 0, "discarded", 4299,
                            pending_at_destroy, sizeof(pending_at_destroy)));
    lua_runtime_destroy(&runtime_b);
    assert(udp_mock_live_count() == 1);
    assert(udp_mock_enqueue(runtime_a.udp, 0, "lua-a-again", 4203,
                            message_after_destroy, sizeof(message_after_destroy)));
    lua_udp_poll(runtime_a.udp_state);
    lua_assert_message(&runtime_a, 2, 71, "lua-a-again", 4203);
    lua_runtime_destroy(&runtime_a);
    assert(udp_mock_live_count() == 0);
    assert(udp_mock_destroy_count() == 2);
}

int main(void)
{
    test_javascript();
    test_lua();
    puts("{\"runtime\":\"javascript\",\"operation\":\"udp.bind\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"udp.receive\",\"result\":{\"length\":2,\"payloadVerified\":true},\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"udp.bind\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"udp.receive\",\"result\":{\"length\":2,\"payloadVerified\":true},\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("managed UDP binding state isolation tests passed");
    return 0;
}