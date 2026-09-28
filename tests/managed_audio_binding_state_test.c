#include "audio/js_audio_bindings.h"
#include "audio/lua_audio_bindings.h"
#include "tests/audio_mock.h"

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
    JsAudioContext *audio;
} JsAudioRuntime;

typedef struct
{
    lua_State *state;
    LuaAudioContext *audio;
} LuaAudioRuntime;

static void js_eval_or_fail(JsAudioRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "audio_binding_state_test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript audio isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_runtime_create(JsAudioRuntime *runtime, const char *asset_root)
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
    runtime->audio = js_audio_init(runtime->context, asset_root);
    assert(runtime->audio);
}

static void js_runtime_destroy(JsAudioRuntime *runtime)
{
    js_audio_cleanup(runtime->audio);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
}

static void js_set_gain(JsAudioRuntime *runtime, double gain)
{
    char script[256];
    snprintf(script, sizeof(script), "sys.audio.setMasterGain(%.2f);", gain);
    js_eval_or_fail(runtime, script);
}

static void js_assert_state(JsAudioRuntime *runtime, double gain,
                            int expected_root_id)
{
    char script[512];
    snprintf(script, sizeof(script),
             "if (sys.audio.getMasterGain() !== %.2f) throw Error('wrong gain');\n"
             "if (sys.audio.loadBuffer('tone.wav') !== %d) throw Error('wrong asset root');\n",
             gain, expected_root_id);
    js_eval_or_fail(runtime, script);
}

static void test_javascript(void)
{
    JsAudioRuntime runtime_a;
    JsAudioRuntime runtime_b;

    audio_mock_reset();
    js_runtime_create(&runtime_a, "managed-js-a");
    js_runtime_create(&runtime_b, "managed-js-b");
    assert(audio_mock_create_count() == 0);

    js_set_gain(&runtime_a, 0.25);
    js_set_gain(&runtime_b, 0.75);
    assert(audio_mock_create_count() == 2);
    js_assert_state(&runtime_a, 0.25,
                    audio_mock_asset_root_id("managed-js-a"));
    js_assert_state(&runtime_b, 0.75,
                    audio_mock_asset_root_id("managed-js-b"));

    js_runtime_destroy(&runtime_b);
    assert(audio_mock_destroy_count() == 1);
    assert(audio_mock_live_count() == 1);
    js_assert_state(&runtime_a, 0.25,
                    audio_mock_asset_root_id("managed-js-a"));
    js_runtime_destroy(&runtime_a);
    assert(audio_mock_destroy_count() == 2);
    assert(audio_mock_live_count() == 0);
}

static void lua_eval_or_fail(LuaAudioRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua audio isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaAudioRuntime *runtime, const char *asset_root)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");
    runtime->audio = lua_audio_init(runtime->state, asset_root);
    assert(runtime->audio);
}

static void lua_runtime_destroy(LuaAudioRuntime *runtime)
{
    lua_audio_cleanup(runtime->audio);
    lua_close(runtime->state);
}

static void lua_set_gain(LuaAudioRuntime *runtime, double gain)
{
    char script[256];
    snprintf(script, sizeof(script), "sys.audio.setMasterGain(%.2f)", gain);
    lua_eval_or_fail(runtime, script);
}

static void lua_assert_state(LuaAudioRuntime *runtime, double gain,
                             int expected_root_id)
{
    char script[512];
    snprintf(script, sizeof(script),
             "assert(sys.audio.getMasterGain() == %.2f)\n"
             "assert(sys.audio.loadBuffer('tone.wav') == %d)\n",
             gain, expected_root_id);
    lua_eval_or_fail(runtime, script);
}

static void test_lua(void)
{
    LuaAudioRuntime runtime_a;
    LuaAudioRuntime runtime_b;

    audio_mock_reset();
    lua_runtime_create(&runtime_a, "managed-lua-a");
    lua_runtime_create(&runtime_b, "managed-lua-b");
    assert(audio_mock_create_count() == 0);

    lua_set_gain(&runtime_a, 0.25);
    lua_set_gain(&runtime_b, 0.75);
    assert(audio_mock_create_count() == 2);
    lua_assert_state(&runtime_a, 0.25,
                     audio_mock_asset_root_id("managed-lua-a"));
    lua_assert_state(&runtime_b, 0.75,
                     audio_mock_asset_root_id("managed-lua-b"));

    lua_runtime_destroy(&runtime_b);
    assert(audio_mock_destroy_count() == 1);
    assert(audio_mock_live_count() == 1);
    lua_assert_state(&runtime_a, 0.25,
                     audio_mock_asset_root_id("managed-lua-a"));
    lua_runtime_destroy(&runtime_a);
    assert(audio_mock_destroy_count() == 2);
    assert(audio_mock_live_count() == 0);
}

int main(void)
{
    test_javascript();
    test_lua();
    puts("managed audio binding state isolation tests passed");
    return 0;
}