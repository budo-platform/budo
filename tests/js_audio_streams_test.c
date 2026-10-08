#include "audio/js_audio_bindings.h"
#include "tests/audio_mock.h"

#include "quickjs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static JSRuntime *runtime;
static JSContext *context;
static JsAudioContext *audio;

static JSValue eval(const char *script)
{
    JSValue result = JS_Eval(context, script, strlen(script), "js_audio_streams_test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(context);
        const char *message = JS_ToCString(context, exception);
        fprintf(stderr, "JavaScript failure: %s\n", message ? message : "exception");
        JS_FreeCString(context, message);
        JS_FreeValue(context, exception);
        assert(0);
    }
    return result;
}

static double eval_number(const char *script)
{
    JSValue result = eval(script);
    double value = 0;
    JS_ToFloat64(context, &value, result);
    JS_FreeValue(context, result);
    return value;
}

static void setup(void)
{
    audio_mock_reset();
    runtime = JS_NewRuntime();
    context = JS_NewContext(runtime);
    JSValue global = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, global, "sys", JS_NewObject(context));
    JS_FreeValue(context, global);
    audio = js_audio_init(context, ".");
    assert(audio);
}

static void teardown(void)
{
    js_audio_cleanup(audio);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

static void test_output_callbacks_fill_the_queue(void)
{
    setup();
    
    JS_FreeValue(context, eval(
        "var calls = 0, last = null, value = 0.1;\n"
        "var out = sys.audio.openOutput({ channels: 1, latencyMs: 20 }, (buffer, info) => {\n"
        "  calls++; last = { frames: info.frames, channels: info.channels, rate: info.sampleRate, time: info.time,\n"
        "                    underruns: info.underruns, length: buffer.length, silent: buffer.every(v => v === 0) };\n"
        "  buffer.fill(value);\n"
        "});\n"));
    assert(eval_number("out") == 0);
    assert(eval_number("sys.audio.getSampleRate()") == 48000);
    js_audio_poll(audio);
    assert(eval_number("calls") == 3);
    assert(eval_number("last.frames") == 256 && eval_number("last.channels") == 1 && eval_number("last.rate") == 48000);
    assert(eval_number("last.length") == 256 && eval_number("last.silent ? 1 : 0") == 1);
    assert(fabs(eval_number("last.time") - 512.0 / 48000.0) < 1e-9);
    assert(js_audio_has_pending_work(audio) && js_audio_max_idle_ms(audio) >= 0);

    AudioContext *mock = audio_mock_last_context();
    float stereo[2 * 512];
    memset(stereo, 0, sizeof(stereo));
    audio_mock_mix(mock, stereo, 512);
    assert(fabsf(stereo[0] - 0.1f) < 1e-6f && fabsf(stereo[1023] - 0.1f) < 1e-6f);
    
    js_audio_poll(audio);
    assert(eval_number("calls") == 5);

    JS_FreeValue(context, eval("sys.audio.closeOutput(out);"));
    js_audio_poll(audio);
    assert(eval_number("calls") == 5 && !js_audio_has_pending_work(audio));
    teardown();
}

static void test_a_throwing_callback_stops(void)
{
    setup();
    JS_FreeValue(context, eval(
        "var calls = 0;\n"
        "sys.audio.openOutput(() => { calls++; throw new Error('boom'); });\n"));
    js_audio_poll(audio);
    js_audio_poll(audio);
    assert(eval_number("calls") == 1);
    JSValue error = eval("sys.audio.getError()");
    const char *text = JS_ToCString(context, error);
    assert(text && strstr(text, "boom"));
    JS_FreeCString(context, text);
    JS_FreeValue(context, error);
    teardown();
}

static void test_a_callback_may_close_its_stream(void)
{
    setup();
    JS_FreeValue(context, eval(
        "var calls = 0, s = sys.audio.openOutput({ latencyMs: 100 }, () => { calls++; sys.audio.closeOutput(s); });\n"));
    js_audio_poll(audio);
    assert(eval_number("calls") == 1 && !js_audio_has_pending_work(audio));
    teardown();
}

static void test_input_callbacks_receive_captures(void)
{
    setup();
    JS_FreeValue(context, eval(
        "var chunks = 0, first = 0, rate = 0;\n"
        "var mic = sys.audio.openInput({ channels: 1 }, (samples, info) => {\n"
        "  if (!chunks) first = samples[0];\n"
        "  chunks++; rate = info.sampleRate;\n"
        "});\n"));
    AudioContext *mock = audio_mock_last_context();
    float stereo[2 * 600];
    for (int i = 0; i < 600; i++)
    {
        stereo[i * 2] = 0.2f;
        stereo[i * 2 + 1] = 0.4f;
    }
    audio_mock_capture(mock, stereo, 600, 2);
    js_audio_poll(audio);
    
    assert(eval_number("chunks") == 2 && eval_number("rate") == 48000);
    assert(fabs(eval_number("first") - 0.3) < 1e-6);
    JS_FreeValue(context, eval("sys.audio.closeInput(mic);"));
    teardown();
}

static void test_argument_errors(void)
{
    setup();
    JS_FreeValue(context, eval(
        "function throws(f, type) { try { f(); } catch (e) { return e instanceof type; } return false; }\n"
        "if (!throws(() => sys.audio.openOutput({}), TypeError)) throw Error('callback required');\n"
        "if (!throws(() => sys.audio.openOutput({ channels: 3 }, () => {}), RangeError)) throw Error('channels');\n"
        "if (!throws(() => sys.audio.openInput({ latencyMs: 0 }, () => {}), RangeError)) throw Error('latency');\n"
        "if (!throws(() => sys.audio.decode(99, new Float32Array(4)), TypeError)) throw Error('decoder handle');\n"
        "if (sys.audio.openDecoder('assets/missing.mp3') !== -1) throw Error('missing file');\n"));
    teardown();
}

int main(void)
{
    test_output_callbacks_fill_the_queue();
    test_a_throwing_callback_stops();
    test_a_callback_may_close_its_stream();
    test_input_callbacks_receive_captures();
    test_argument_errors();
    puts("js_audio_streams_test: ok");
    return 0;
}