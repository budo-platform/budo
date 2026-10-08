#include "midi/js_midi_bindings.h"
#include "midi/lua_midi_bindings.h"
#include "tests/midi_mock.h"
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
    JsMidiContext *midi_state;
    MidiContext *midi;
    UdpContext *udp;
    RtpMidiContext *rtpmidi;
    int session;
} JsMidiRuntime;

typedef struct
{
    lua_State *state;
    LuaMidiContext *midi_state;
    MidiContext *midi;
    UdpContext *udp;
    RtpMidiContext *rtpmidi;
    int session;
} LuaMidiRuntime;

static void js_eval_or_fail(JsMidiRuntime *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script),
                             "managed_midi_binding_state_test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript MIDI isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(0);
    }
    JS_FreeValue(runtime->context, result);
}

static void js_runtime_create(JsMidiRuntime *runtime, const char *session_name)
{
    JSValue global;
    char script[1024];

    memset(runtime, 0, sizeof(*runtime));
    runtime->runtime = JS_NewRuntime();
    assert(runtime->runtime);
    runtime->context = JS_NewContext(runtime->runtime);
    assert(runtime->context);
    global = JS_GetGlobalObject(runtime->context);
    assert(JS_SetPropertyStr(runtime->context, global, "sys",
                             JS_NewObject(runtime->context)) >= 0);
    JS_FreeValue(runtime->context, global);

    runtime->udp = udp_create();
    assert(runtime->udp);
    runtime->rtpmidi = rtpmidi_create(runtime->udp);
    assert(runtime->rtpmidi);
    runtime->midi_state = js_midi_init(runtime->context);
    assert(runtime->midi_state);
    js_midi_set_rtpmidi(runtime->midi_state, runtime->rtpmidi);

    snprintf(script, sizeof(script),
             "globalThis.localCount = 0;\n"
             "globalThis.sysexCount = 0;\n"
             "globalThis.rtpCount = 0;\n"
             "globalThis.lastLocal = -1;\n"
             "globalThis.lastSysex = -1;\n"
             "globalThis.lastRtp = -1;\n"
             "globalThis.order = '';\n"
             "globalThis.inputHandle = sys.midi.openInput(0, message => {\n"
             "  if (message.status === sys.midi.SYSEX) {\n"
             "    sysexCount++; lastSysex = message.data[1]; order += 's';\n"
             "  } else { localCount++; lastLocal = message.data1; order += 'm'; }\n"
             "  order = order.slice(-8);\n"
             "});\n"
             "globalThis.sessionHandle = sys.midi.createSession('%s', 0);\n"
             "sys.midi.onSessionMessage(sessionHandle, message => {\n"
             "  rtpCount++; lastRtp = message.data1;\n"
             "});\n",
             session_name);
    js_eval_or_fail(runtime, script);
    runtime->midi = midi_mock_context(midi_mock_live_count() - 1);
    runtime->session = 0;
    assert(runtime->midi);
    assert(rtpmidi_mock_callback_attached(runtime->rtpmidi,
                                          runtime->session));
}

static void js_assert_counts(JsMidiRuntime *runtime,
                             int local_count, int sysex_count, int rtp_count,
                             int last_local, int last_sysex, int last_rtp)
{
    char script[768];
    snprintf(script, sizeof(script),
             "if (localCount !== %d || sysexCount !== %d || rtpCount !== %d)\n"
             "  throw Error('wrong callback counts');\n"
             "if (lastLocal !== %d || lastSysex !== %d || lastRtp !== %d)\n"
             "  throw Error('wrong callback payload');\n",
             local_count, sysex_count, rtp_count,
             last_local, last_sysex, last_rtp);
    js_eval_or_fail(runtime, script);
}

static void js_runtime_destroy(JsMidiRuntime *runtime)
{
    MidiMessage message = {MIDI_NOTE_ON, 1, 2, 3};
    js_midi_cleanup(runtime->midi_state);
    runtime->midi_state = NULL;
    assert(!midi_mock_emit(runtime->midi, 0, &message));
    assert(!rtpmidi_mock_callback_attached(runtime->rtpmidi,
                                           runtime->session));
    rtpmidi_destroy(runtime->rtpmidi);
    udp_destroy(runtime->udp);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
}

static void test_javascript(void)
{
    JsMidiRuntime runtime_a;
    JsMidiRuntime runtime_b;
    MidiMessage message_a = {MIDI_NOTE_ON, 11, 1, 100};
    MidiMessage message_b = {MIDI_NOTE_ON, 22, 2, 200};
    MidiMessage rtp_a = {MIDI_CONTROL_CHANGE, 31, 3, 300};
    MidiMessage rtp_b = {MIDI_CONTROL_CHANGE, 32, 4, 400};
    const uint8_t sysex_a[] = {0xf0, 41, 0xf7};
    const uint8_t sysex_b[] = {0xf0, 42, 0xf7};

    udp_mock_reset();
    midi_mock_reset();
    js_runtime_create(&runtime_a, "js-a");
    js_runtime_create(&runtime_b, "js-b");
    assert(midi_mock_live_count() == 2);
    assert(rtpmidi_mock_live_count() == 2);
    assert(strcmp(rtpmidi_mock_session_name(runtime_a.rtpmidi, 0), "js-a") == 0);
    assert(strcmp(rtpmidi_mock_session_name(runtime_b.rtpmidi, 0), "js-b") == 0);

    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    assert(midi_mock_emit(runtime_b.midi, 0, &message_b));
    assert(midi_mock_emit_sysex(runtime_a.midi, 0,
                                sysex_a, sizeof(sysex_a)));
    assert(midi_mock_emit_sysex(runtime_b.midi, 0,
                                sysex_b, sizeof(sysex_b)));
    assert(rtpmidi_mock_emit(runtime_a.rtpmidi, 0, &rtp_a));
    assert(rtpmidi_mock_emit(runtime_b.rtpmidi, 0, &rtp_b));

    js_midi_poll(runtime_a.midi_state);
    js_assert_counts(&runtime_a, 1, 1, 1, 11, 41, 31);
    js_assert_counts(&runtime_b, 0, 0, 0, -1, -1, -1);
    js_midi_poll(runtime_b.midi_state);
    js_assert_counts(&runtime_b, 1, 1, 1, 22, 42, 32);

    js_eval_or_fail(&runtime_a, "order = '';");
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    assert(midi_mock_emit_sysex(runtime_a.midi, 0, sysex_a, sizeof(sysex_a)));
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    assert(midi_mock_emit_sysex(runtime_a.midi, 0, sysex_a, sizeof(sysex_a)));
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    js_midi_poll(runtime_a.midi_state);
    js_eval_or_fail(&runtime_a, "if (order !== 'msmsm') throw Error('arrival order lost: ' + order);");
    js_assert_counts(&runtime_a, 4, 3, 1, 11, 41, 31);

    for (int index = 0; index < 1030; index++)
        assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    for (int index = 0; index < 260; index++)
        assert(rtpmidi_mock_emit(runtime_a.rtpmidi, 0, &rtp_a));
    for (int index = 0; index < 18; index++)
        assert(midi_mock_emit_sysex(runtime_a.midi, 0,
                                    sysex_a, sizeof(sysex_a)));
    assert(js_midi_dropped_messages(runtime_a.midi_state) == 6);
    assert(js_midi_dropped_sysex(runtime_a.midi_state) == 18);
    assert(js_midi_dropped_rtpmidi(runtime_a.midi_state) == 4);
    assert(js_midi_dropped_messages(runtime_b.midi_state) == 0);
    js_midi_poll(runtime_a.midi_state);
    js_assert_counts(&runtime_a, 1028, 3, 257, 11, 41, 31);

    for (int index = 0; index < 34; index++)
        assert(midi_mock_emit_sysex(runtime_a.midi, 0,
                                    sysex_a, sizeof(sysex_a)));
    assert(js_midi_dropped_sysex(runtime_a.midi_state) == 20);
    js_midi_poll(runtime_a.midi_state);
    js_assert_counts(&runtime_a, 1028, 35, 257, 11, 41, 31);

    js_runtime_destroy(&runtime_b);
    assert(midi_mock_live_count() == 1);
    assert(rtpmidi_mock_live_count() == 1);
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    js_midi_poll(runtime_a.midi_state);
    js_assert_counts(&runtime_a, 1029, 35, 257, 11, 41, 31);
    js_runtime_destroy(&runtime_a);
    assert(midi_mock_destroy_count() == 2);
    assert(rtpmidi_mock_destroy_count() == 2);
    assert(udp_mock_destroy_count() == 2);
}

static void lua_eval_or_fail(LuaMidiRuntime *runtime, const char *script)
{
    if (luaL_dostring(runtime->state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua MIDI isolation failure: %s\n",
                lua_tostring(runtime->state, -1));
        assert(0);
    }
}

static void lua_runtime_create(LuaMidiRuntime *runtime,
                               const char *session_name)
{
    char script[1024];
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = luaL_newstate();
    assert(runtime->state);
    luaL_openlibs(runtime->state);
    lua_newtable(runtime->state);
    lua_setglobal(runtime->state, "sys");

    runtime->udp = udp_create();
    assert(runtime->udp);
    runtime->rtpmidi = rtpmidi_create(runtime->udp);
    assert(runtime->rtpmidi);
    runtime->midi_state = lua_midi_init(runtime->state);
    assert(runtime->midi_state);
    lua_midi_set_rtpmidi(runtime->midi_state, runtime->rtpmidi);

    snprintf(script, sizeof(script),
             "localCount, sysexCount, rtpCount = 0, 0, 0\n"
             "lastLocal, lastSysex, lastRtp = -1, -1, -1\n"
             "order = ''\n"
             "inputHandle = sys.midi.openInput(0, function(message)\n"
             "  if message.status == sys.midi.SYSEX then\n"
             "    sysexCount = sysexCount + 1; lastSysex = message.data[2]; order = order .. 's'\n"
             "  else localCount = localCount + 1; lastLocal = message.data1; order = order .. 'm' end\n"
             "  order = string.sub(order, -8)\n"
             "end)\n"
             "sessionHandle = sys.midi.createSession('%s', 0)\n"
             "sys.midi.onSessionMessage(sessionHandle, function(message)\n"
             "  rtpCount = rtpCount + 1; lastRtp = message.data1\n"
             "end)\n",
             session_name);
    lua_eval_or_fail(runtime, script);
    runtime->midi = midi_mock_context(midi_mock_live_count() - 1);
    runtime->session = 0;
    assert(runtime->midi);
    assert(rtpmidi_mock_callback_attached(runtime->rtpmidi,
                                          runtime->session));
}

static void lua_assert_counts(LuaMidiRuntime *runtime,
                              int local_count, int sysex_count, int rtp_count,
                              int last_local, int last_sysex, int last_rtp)
{
    char script[512];
    snprintf(script, sizeof(script),
             "assert(localCount == %d and sysexCount == %d and rtpCount == %d)\n"
             "assert(lastLocal == %d and lastSysex == %d and lastRtp == %d)\n",
             local_count, sysex_count, rtp_count,
             last_local, last_sysex, last_rtp);
    lua_eval_or_fail(runtime, script);
}

static void lua_runtime_destroy(LuaMidiRuntime *runtime)
{
    MidiMessage message = {MIDI_NOTE_ON, 1, 2, 3};
    lua_midi_cleanup(runtime->midi_state);
    runtime->midi_state = NULL;
    assert(!midi_mock_emit(runtime->midi, 0, &message));
    assert(!rtpmidi_mock_callback_attached(runtime->rtpmidi,
                                           runtime->session));
    rtpmidi_destroy(runtime->rtpmidi);
    udp_destroy(runtime->udp);
    lua_close(runtime->state);
}

static void test_lua(void)
{
    LuaMidiRuntime runtime_a;
    LuaMidiRuntime runtime_b;
    MidiMessage message_a = {MIDI_NOTE_ON, 51, 1, 100};
    MidiMessage message_b = {MIDI_NOTE_ON, 52, 2, 200};
    MidiMessage rtp_a = {MIDI_CONTROL_CHANGE, 61, 3, 300};
    MidiMessage rtp_b = {MIDI_CONTROL_CHANGE, 62, 4, 400};
    const uint8_t sysex_a[] = {0xf0, 71, 0xf7};
    const uint8_t sysex_b[] = {0xf0, 72, 0xf7};

    udp_mock_reset();
    midi_mock_reset();
    lua_runtime_create(&runtime_a, "lua-a");
    lua_runtime_create(&runtime_b, "lua-b");

    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    assert(midi_mock_emit(runtime_b.midi, 0, &message_b));
    assert(midi_mock_emit_sysex(runtime_a.midi, 0,
                                sysex_a, sizeof(sysex_a)));
    assert(midi_mock_emit_sysex(runtime_b.midi, 0,
                                sysex_b, sizeof(sysex_b)));
    assert(rtpmidi_mock_emit(runtime_a.rtpmidi, 0, &rtp_a));
    assert(rtpmidi_mock_emit(runtime_b.rtpmidi, 0, &rtp_b));

    lua_midi_poll(runtime_a.midi_state);
    lua_assert_counts(&runtime_a, 1, 1, 1, 51, 71, 61);
    lua_assert_counts(&runtime_b, 0, 0, 0, -1, -1, -1);
    lua_midi_poll(runtime_b.midi_state);
    lua_assert_counts(&runtime_b, 1, 1, 1, 52, 72, 62);

    lua_eval_or_fail(&runtime_a, "order = ''");
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    assert(midi_mock_emit_sysex(runtime_a.midi, 0, sysex_a, sizeof(sysex_a)));
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    assert(midi_mock_emit_sysex(runtime_a.midi, 0, sysex_a, sizeof(sysex_a)));
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    lua_midi_poll(runtime_a.midi_state);
    lua_eval_or_fail(&runtime_a, "assert(order == 'msmsm', 'arrival order lost: ' .. order)");
    lua_assert_counts(&runtime_a, 4, 3, 1, 51, 71, 61);

    for (int index = 0; index < 1030; index++)
        assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    for (int index = 0; index < 260; index++)
        assert(rtpmidi_mock_emit(runtime_a.rtpmidi, 0, &rtp_a));
    for (int index = 0; index < 18; index++)
        assert(midi_mock_emit_sysex(runtime_a.midi, 0,
                                    sysex_a, sizeof(sysex_a)));
    assert(lua_midi_dropped_messages(runtime_a.midi_state) == 6);
    assert(lua_midi_dropped_sysex(runtime_a.midi_state) == 18);
    assert(lua_midi_dropped_rtpmidi(runtime_a.midi_state) == 4);
    assert(lua_midi_dropped_messages(runtime_b.midi_state) == 0);
    lua_midi_poll(runtime_a.midi_state);
    lua_assert_counts(&runtime_a, 1028, 3, 257, 51, 71, 61);

    for (int index = 0; index < 34; index++)
        assert(midi_mock_emit_sysex(runtime_a.midi, 0,
                                    sysex_a, sizeof(sysex_a)));
    assert(lua_midi_dropped_sysex(runtime_a.midi_state) == 20);
    lua_midi_poll(runtime_a.midi_state);
    lua_assert_counts(&runtime_a, 1028, 35, 257, 51, 71, 61);

    lua_runtime_destroy(&runtime_b);
    assert(midi_mock_live_count() == 1);
    assert(rtpmidi_mock_live_count() == 1);
    assert(midi_mock_emit(runtime_a.midi, 0, &message_a));
    lua_midi_poll(runtime_a.midi_state);
    lua_assert_counts(&runtime_a, 1029, 35, 257, 51, 71, 61);
    lua_runtime_destroy(&runtime_a);
    assert(midi_mock_destroy_count() == 2);
    assert(rtpmidi_mock_destroy_count() == 2);
    assert(udp_mock_destroy_count() == 2);
}

int main(void)
{
    test_javascript();
    test_lua();
    puts("{\"runtime\":\"javascript\",\"operation\":\"midi.localIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"midi.rtpmidiIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"midi.cleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"midi.localIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"midi.rtpmidiIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"midi.cleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("managed MIDI binding state isolation tests passed");
    return 0;
}