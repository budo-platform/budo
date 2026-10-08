#include "js_audio_bindings.h"
#include "audio_decoder.h"
#include "audio_dsp.h"
#include "audio_service.h"
#include "file/file_wrapper.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#endif

#define JS_AUDIO_MAX_DECODERS 32

typedef struct JsAudioStream
{
    bool active;
    bool failed; 
    int channels;
    JSValue callback;
    JSValue buffer; 
    JSValue info;
    float *samples;   
    uint64_t frames;  
} JsAudioStream;

struct JsAudioContext
{
    AudioContext *audio_ctx;
    bool lazy_initialized;
    char asset_root[4096];
    JSContext *js;
    FileContext *const *files; 
    JsAudioStream outputs[AUDIO_MAX_OUTPUT_STREAMS];
    JsAudioStream inputs[AUDIO_MAX_INPUT_STREAMS];
    AudioStreamDecoder *decoders[JS_AUDIO_MAX_DECODERS];
    char error[512]; 
};

static JsAudioContext *js_audio_binding_state(JSContext *ctx,
                                              JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    JsAudioContext *state = NULL;

    if (data && size == sizeof(state))
        memcpy(&state, data, sizeof(state));
    return state;
}

static AudioContext *js_audio_context(JSContext *ctx, JSValueConst *func_data)
{
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    if (!state)
        return NULL;
    if (!state->lazy_initialized)
    {
        state->lazy_initialized = true;
        state->audio_ctx = audio_create();
        if (state->audio_ctx)
            audio_set_asset_root(state->audio_ctx, state->asset_root);
        else
            fprintf(stderr, "Warning: Failed to create audio context\n");
    }
    return state->audio_ctx;
}

#define JS_AUDIO_CALLBACK(name)                                          \
    static JSValue name(JSContext *ctx, JSValueConst this_val, int argc, \
                        JSValueConst *argv, int magic, JSValueConst *func_data)

#define JS_AUDIO_CONTEXT()                                        \
    AudioContext *g_audio_ctx = js_audio_context(ctx, func_data); \
    (void)magic

JS_AUDIO_CALLBACK(js_audio_start)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_AUDIO_CONTEXT();
    if (g_audio_ctx)
    {
        audio_start(g_audio_ctx);
    }
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_stop)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_AUDIO_CONTEXT();
    if (g_audio_ctx)
    {
        audio_stop(g_audio_ctx);
    }
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_is_playing)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx)
        return JS_FALSE;
    return JS_NewBool(ctx, audio_is_playing(g_audio_ctx));
}

JS_AUDIO_CALLBACK(js_audio_set_master_gain)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    double gain;
    JS_ToFloat64(ctx, &gain, argv[0]);
    audio_set_master_gain(g_audio_ctx, (float)gain);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_get_master_gain)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx)
        return JS_NewFloat64(ctx, 0.0);
    return JS_NewFloat64(ctx, audio_get_master_gain(g_audio_ctx));
}

JS_AUDIO_CALLBACK(js_audio_create_oscillator)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx)
        return JS_NewInt32(ctx, -1);
    ApiError error;
    return JS_NewInt32(ctx, audio_service_create_oscillator(g_audio_ctx, &error));
}

JS_AUDIO_CALLBACK(js_audio_destroy_oscillator)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    audio_destroy_oscillator(g_audio_ctx, id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_set_type)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 2)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);

    AudioWaveType type = AUDIO_WAVE_SINE;
    if (JS_IsNumber(argv[1]))
    {
        int type_int;
        JS_ToInt32(ctx, &type_int, argv[1]);
        type = (AudioWaveType)type_int;
    }
    else if (JS_IsString(argv[1]))
    {
        const char *type_str = JS_ToCString(ctx, argv[1]);
        if (type_str)
        {
            audio_wave_type_from_string(type_str, &type);
            JS_FreeCString(ctx, type_str);
        }
    }

    audio_oscillator_set_type(g_audio_ctx, id, type);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_set_frequency)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 2)
        return JS_UNDEFINED;

    int id;
    double freq;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &freq, argv[1]);
    audio_oscillator_set_frequency(g_audio_ctx, id, (float)freq);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_set_gain)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 2)
        return JS_UNDEFINED;

    int id;
    double gain;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &gain, argv[1]);
    audio_oscillator_set_gain(g_audio_ctx, id, (float)gain);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_set_detune)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 2)
        return JS_UNDEFINED;

    int id;
    double cents;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &cents, argv[1]);
    audio_oscillator_set_detune(g_audio_ctx, id, (float)cents);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_start)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    audio_oscillator_start(g_audio_ctx, id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_stop)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    audio_oscillator_stop(g_audio_ctx, id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_set_envelope)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 5)
        return JS_UNDEFINED;

    int id;
    double attack, decay, sustain, release;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToFloat64(ctx, &attack, argv[1]);
    JS_ToFloat64(ctx, &decay, argv[2]);
    JS_ToFloat64(ctx, &sustain, argv[3]);
    JS_ToFloat64(ctx, &release, argv[4]);
    audio_oscillator_set_envelope(g_audio_ctx, id, (float)attack, (float)decay,
                                  (float)sustain, (float)release);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_note_on)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    audio_oscillator_note_on(g_audio_ctx, id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_oscillator_note_off)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    audio_oscillator_note_off(g_audio_ctx, id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_create_buffer)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 3)
        return JS_NewInt32(ctx, -1);

    int sample_rate, channels, num_samples;
    JS_ToInt32(ctx, &sample_rate, argv[0]);
    JS_ToInt32(ctx, &channels, argv[1]);
    JS_ToInt32(ctx, &num_samples, argv[2]);

    return JS_NewInt32(ctx, audio_create_buffer(g_audio_ctx, sample_rate, channels, num_samples));
}

JS_AUDIO_CALLBACK(js_audio_load_buffer)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_NewInt32(ctx, -1);

    int id = audio_load_buffer(g_audio_ctx, path);
    JS_FreeCString(ctx, path);
    return JS_NewInt32(ctx, id);
}

JS_AUDIO_CALLBACK(js_audio_load_buffer_from_buffer)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    size_t byte_count = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &byte_count, argv[0]);
    if (!data)
        return JS_ThrowTypeError(ctx, "audio.loadBufferFromBuffer requires an ArrayBuffer");

    int id = audio_load_buffer_from_memory(g_audio_ctx, data, byte_count);
    return JS_NewInt32(ctx, id);
}

JS_AUDIO_CALLBACK(js_audio_get_error)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_AUDIO_CONTEXT();
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    if (state && state->error[0])
        return JS_NewString(ctx, state->error);
    if (!g_audio_ctx)
        return JS_NewString(ctx, "Audio context unavailable");
    return JS_NewString(ctx, audio_get_error(g_audio_ctx));
}

JS_AUDIO_CALLBACK(js_audio_buffer_set_data)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 2)
        return JS_UNDEFINED;

    int buffer_id;
    JS_ToInt32(ctx, &buffer_id, argv[0]);

    if (!BUDO_JS_IS_ARRAY(ctx, argv[1]))
        return JS_UNDEFINED;

    JSValue length_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t length;
    JS_ToInt32(ctx, &length, length_val);
    JS_FreeValue(ctx, length_val);

    int offset = 0;
    if (argc >= 3)
    {
        JS_ToInt32(ctx, &offset, argv[2]);
    }

    float *samples = (float *)malloc(length * sizeof(float));
    if (!samples)
        return JS_UNDEFINED;

    for (int32_t i = 0; i < length; i++)
    {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
        double val;
        JS_ToFloat64(ctx, &val, elem);
        samples[i] = (float)val;
        JS_FreeValue(ctx, elem);
    }

    audio_buffer_set_data(g_audio_ctx, buffer_id, samples, offset, length);
    free(samples);

    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_destroy_buffer)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int id;
    JS_ToInt32(ctx, &id, argv[0]);
    audio_destroy_buffer(g_audio_ctx, id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_play_buffer)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    int buffer_id;
    bool loop = false;
    double gain = 1.0;

    JS_ToInt32(ctx, &buffer_id, argv[0]);
    if (argc >= 2)
        loop = JS_ToBool(ctx, argv[1]);
    if (argc >= 3)
        JS_ToFloat64(ctx, &gain, argv[2]);

    return JS_NewInt32(ctx, audio_play_buffer(g_audio_ctx, buffer_id, loop, (float)gain));
}

JS_AUDIO_CALLBACK(js_audio_stop_buffer)
{
    (void)this_val;
    JS_AUDIO_CONTEXT();
    if (!g_audio_ctx || argc < 1)
        return JS_UNDEFINED;

    int playback_id;
    JS_ToInt32(ctx, &playback_id, argv[0]);
    audio_stop_buffer(g_audio_ctx, playback_id);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_midi_to_freq)
{
    (void)this_val;
    (void)magic;
    (void)func_data;
    if (argc < 1)
        return JS_NewFloat64(ctx, 440.0);

    int note;
    JS_ToInt32(ctx, &note, argv[0]);
    return JS_NewFloat64(ctx, audio_midi_to_freq(note));
}

static void js_audio_set_int(JSContext *ctx, JSValueConst object, const char *name, int value)
{
    JS_SetPropertyStr(ctx, object, name, JS_NewInt32(ctx, value));
}

static void js_audio_fail(JsAudioContext *state, const char *message)
{
    snprintf(state->error, sizeof(state->error), "%s", message);
}

static JSValue js_audio_new_float32(JSContext *ctx, size_t count, float **samples)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue constructor = JS_GetPropertyStr(ctx, global, "Float32Array");
    JSValue length = JS_NewInt64(ctx, (int64_t)count);
    JSValue array = JS_CallConstructor(ctx, constructor, 1, &length);
    JS_FreeValue(ctx, length);
    JS_FreeValue(ctx, constructor);
    JS_FreeValue(ctx, global);
    if (JS_IsException(array))
        return array;
    size_t offset = 0, bytes = 0, element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, array, &offset, &bytes, &element);
    size_t size = 0;
    uint8_t *data = JS_IsException(buffer) ? NULL : JS_GetArrayBuffer(ctx, &size, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
    {
        JS_FreeValue(ctx, array);
        return JS_ThrowInternalError(ctx, "audio: could not allocate a sample buffer");
    }
    *samples = (float *)(data + offset);
    return array;
}

static float *js_audio_float32_arg(JSContext *ctx, JSValueConst value, size_t *count)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue constructor = JS_GetPropertyStr(ctx, global, "Float32Array");
    int is_float32 = JS_IsInstanceOf(ctx, value, constructor);
    JS_FreeValue(ctx, constructor);
    JS_FreeValue(ctx, global);
    if (is_float32 != 1)
        return NULL;
    size_t offset = 0, bytes = 0, element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &bytes, &element);
    if (JS_IsException(buffer))
        return NULL;
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return NULL;
    *count = bytes / sizeof(float);
    return (float *)(data + offset);
}

static const uint8_t *js_audio_bytes_arg(JSContext *ctx, JSValueConst value, size_t *size)
{
    uint8_t *data = JS_GetArrayBuffer(ctx, size, value);
    if (data)
        return data;
    JS_FreeValue(ctx, JS_GetException(ctx));
    size_t offset = 0, bytes = 0, element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &bytes, &element);
    if (JS_IsException(buffer))
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return NULL;
    }
    size_t total = 0;
    data = JS_GetArrayBuffer(ctx, &total, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return NULL;
    *size = bytes;
    return data + offset;
}

static double js_audio_number_option(JSContext *ctx, JSValueConst options, const char *name, double fallback)
{
    if (!JS_IsObject(options))
        return fallback;
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    double number = fallback;
    if (!JS_IsUndefined(value) && !JS_IsNull(value))
        JS_ToFloat64(ctx, &number, value);
    JS_FreeValue(ctx, value);
    return number;
}

static void js_audio_stream_release(JSContext *ctx, JsAudioStream *stream)
{
    if (!stream->active)
        return;
    JS_FreeValue(ctx, stream->callback);
    JS_FreeValue(ctx, stream->buffer);
    JS_FreeValue(ctx, stream->info);
    memset(stream, 0, sizeof(*stream));
}

static JSValue js_audio_open_stream(JSContext *ctx, int argc, JSValueConst *argv, JSValueConst *func_data,
                                    bool input)
{
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    AudioContext *audio = js_audio_context(ctx, func_data);
    const char *name = input ? "audio.openInput" : "audio.openOutput";
    JSValueConst options = argc >= 2 ? argv[0] : JS_UNDEFINED;
    JSValueConst callback = argc >= 2 ? argv[1] : (argc >= 1 ? argv[0] : JS_UNDEFINED);
    if (!JS_IsFunction(ctx, callback))
        return JS_ThrowTypeError(ctx, "%s([options], callback): callback must be a function", name);
    if (!JS_IsUndefined(options) && !JS_IsNull(options) && !JS_IsObject(options))
        return JS_ThrowTypeError(ctx, "%s: options must be an object", name);
    if (!state || !audio)
        return JS_NewInt32(ctx, -1);
    state->error[0] = '\0';
    int channels = (int)js_audio_number_option(ctx, options, "channels", input ? 1 : 2);
    
    double latency = js_audio_number_option(ctx, options, "latencyMs", NAN);
    if (channels != 1 && channels != 2)
        return JS_ThrowRangeError(ctx, "%s: channels must be 1 or 2", name);
    if (isnan(latency))
        latency = 0.0;
    else if (!(latency >= 1.0 && latency <= 2000.0))
        return JS_ThrowRangeError(ctx, "%s: latencyMs must be between 1 and 2000", name);
    int id = input ? audio_input_open(audio, channels, latency) : audio_output_open(audio, channels, latency);
    if (id < 0)
        return JS_NewInt32(ctx, -1);
    JsAudioStream *stream = input ? &state->inputs[id] : &state->outputs[id];
    js_audio_stream_release(ctx, stream);
    float *samples = NULL;
    JSValue buffer = js_audio_new_float32(ctx, (size_t)AUDIO_STREAM_CHUNK_FRAMES * (size_t)channels, &samples);
    if (JS_IsException(buffer))
    {
        if (input)
            audio_input_close(audio, id);
        else
            audio_output_close(audio, id);
        return buffer;
    }
    stream->active = true;
    stream->channels = channels;
    stream->callback = JS_DupValue(ctx, callback);
    stream->buffer = buffer;
    stream->samples = samples;
    stream->info = JS_NewObject(ctx);
    return JS_NewInt32(ctx, id);
}

static JSValue js_audio_close_stream(JSContext *ctx, int argc, JSValueConst *argv, JSValueConst *func_data,
                                     bool input)
{
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    int id = -1;
    if (argc < 1 || JS_ToInt32(ctx, &id, argv[0]) < 0)
        return JS_EXCEPTION;
    int count = input ? AUDIO_MAX_INPUT_STREAMS : AUDIO_MAX_OUTPUT_STREAMS;
    if (!state || id < 0 || id >= count)
        return JS_UNDEFINED;
    JsAudioStream *stream = input ? &state->inputs[id] : &state->outputs[id];
    if (!stream->active)
        return JS_UNDEFINED;
    if (input)
        audio_input_close(state->audio_ctx, id);
    else
        audio_output_close(state->audio_ctx, id);
    js_audio_stream_release(ctx, stream);
    return JS_UNDEFINED;
}

JS_AUDIO_CALLBACK(js_audio_open_output)
{
    (void)this_val, (void)magic;
    return js_audio_open_stream(ctx, argc, argv, func_data, false);
}

JS_AUDIO_CALLBACK(js_audio_close_output)
{
    (void)this_val, (void)magic;
    return js_audio_close_stream(ctx, argc, argv, func_data, false);
}

JS_AUDIO_CALLBACK(js_audio_open_input)
{
    (void)this_val, (void)magic;
    return js_audio_open_stream(ctx, argc, argv, func_data, true);
}

JS_AUDIO_CALLBACK(js_audio_close_input)
{
    (void)this_val, (void)magic;
    return js_audio_close_stream(ctx, argc, argv, func_data, true);
}

JS_AUDIO_CALLBACK(js_audio_get_sample_rate)
{
    (void)this_val, (void)argc, (void)argv;
    JS_AUDIO_CONTEXT();
    return JS_NewInt32(ctx, g_audio_ctx ? audio_stream_sample_rate(g_audio_ctx) : 0);
}

static bool js_audio_call_stream(JsAudioContext *state, JsAudioStream *stream, bool input, int id)
{
    JSContext *ctx = state->js;
    AudioStreamStats stats;
    memset(&stats, 0, sizeof(stats));
    audio_stream_get_stats(state->audio_ctx, input, id, &stats);
    int rate = stats.sample_rate > 0 ? stats.sample_rate : audio_stream_sample_rate(state->audio_ctx);
    js_audio_set_int(ctx, stream->info, "frames", AUDIO_STREAM_CHUNK_FRAMES);
    js_audio_set_int(ctx, stream->info, "channels", stream->channels);
    js_audio_set_int(ctx, stream->info, "sampleRate", rate);
    JS_SetPropertyStr(ctx, stream->info, "time",
                      JS_NewFloat64(ctx, rate > 0 ? (double)stream->frames / (double)rate : 0.0));
    JS_SetPropertyStr(ctx, stream->info, "latency",
                      JS_NewFloat64(ctx, rate > 0 ? (double)stats.latency_frames / (double)rate : 0.0));
    JS_SetPropertyStr(ctx, stream->info, input ? "overruns" : "underruns",
                      JS_NewFloat64(ctx, (double)stats.glitches));
    JSValue args[2] = {JS_DupValue(ctx, stream->buffer), JS_DupValue(ctx, stream->info)};
    JSValue result = JS_Call(ctx, stream->callback, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    if (JS_IsException(result))
    {
        JSValue error = JS_GetException(ctx);
        const char *text = JS_ToCString(ctx, error);
        fprintf(stderr, "sys.audio %s callback threw: %s (no more calls)\n", input ? "input" : "output",
                text ? text : "error");
        snprintf(state->error, sizeof(state->error), "the %s callback threw: %s", input ? "input" : "output",
                 text ? text : "error");
        JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, error);
        if (stream->active)
            stream->failed = true;
        return false;
    }
    JS_FreeValue(ctx, result);
    stream->frames += AUDIO_STREAM_CHUNK_FRAMES;
    return true;
}

void js_audio_poll(JsAudioContext *state)
{
    if (!state || !state->audio_ctx || !audio_streams_active(state->audio_ctx))
        return;
    AudioContext *audio = state->audio_ctx;
    const size_t chunk = AUDIO_STREAM_CHUNK_FRAMES;
    for (int id = 0; id < AUDIO_MAX_OUTPUT_STREAMS; id++)
    {

        for (int calls = 0; calls < 64; calls++)
        {
            JsAudioStream *stream = &state->outputs[id];
            if (!stream->active || stream->failed || audio_output_wanted(audio, id) < (int)chunk)
                break;
            memset(stream->samples, 0, chunk * (size_t)stream->channels * sizeof(float));
            if (!js_audio_call_stream(state, stream, false, id) || !stream->active)
                break;
            audio_output_write(audio, id, stream->samples, (int)chunk);
        }
    }
    for (int id = 0; id < AUDIO_MAX_INPUT_STREAMS; id++)
    {
        for (int calls = 0; calls < 64; calls++)
        {
            JsAudioStream *stream = &state->inputs[id];
            if (!stream->active || stream->failed || audio_input_available(audio, id) < (int)chunk)
                break;
            audio_input_read(audio, id, stream->samples, (int)chunk);
            if (!js_audio_call_stream(state, stream, true, id))
                break;
        }
    }
    audio_streams_serviced(audio);
}

bool js_audio_has_pending_work(JsAudioContext *state)
{
    return state && state->audio_ctx && audio_streams_active(state->audio_ctx);
}

double js_audio_max_idle_ms(JsAudioContext *state)
{

    return js_audio_has_pending_work(state) ? 4.0 : -1.0;
}

static AudioStreamDecoder *js_audio_decoder_arg(JsAudioContext *state, JSContext *ctx, JSValueConst value,
                                                int *slot)
{
    int handle = 0;
    if (!state || JS_ToInt32(ctx, &handle, value) < 0 || handle < 1 || handle > JS_AUDIO_MAX_DECODERS)
        return NULL;
    if (slot)
        *slot = handle - 1;
    return state->decoders[handle - 1];
}

static JSValue js_audio_store_decoder(JsAudioContext *state, JSContext *ctx, AudioStreamDecoder *decoder)
{
    if (!decoder)
        return JS_NewInt32(ctx, -1);
    for (int i = 0; i < JS_AUDIO_MAX_DECODERS; i++)
        if (!state->decoders[i])
        {
            state->decoders[i] = decoder;
            return JS_NewInt32(ctx, i + 1);
        }
    audio_stream_decoder_close(decoder);
    js_audio_fail(state, "too many open decoders");
    return JS_NewInt32(ctx, -1);
}

JS_AUDIO_CALLBACK(js_audio_open_decoder)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    if (!state)
        return JS_NewInt32(ctx, -1);
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "audio.openDecoder(source, [options]): source is a path or an ArrayBuffer");
    state->error[0] = '\0';
    JSValueConst options = argc >= 2 ? argv[1] : JS_UNDEFINED;
    int rate = (int)js_audio_number_option(ctx, options, "sampleRate", 0);
    int channels = (int)js_audio_number_option(ctx, options, "channels", 2);
    if (channels != 1 && channels != 2)
        return JS_ThrowRangeError(ctx, "audio.openDecoder: channels must be 1 or 2");
    if (rate < 0 || rate > 384000)
        return JS_ThrowRangeError(ctx, "audio.openDecoder: sampleRate must be 0 (the file's) to 384000");
    char error[512] = "";
    AudioStreamDecoder *decoder = NULL;
    if (JS_IsString(argv[0]))
    {
        const char *path = JS_ToCString(ctx, argv[0]);
        if (!path)
            return JS_EXCEPTION;
        FileContext *files = state->files ? *state->files : NULL;
        FileNativeReference file;
        if (!files)
            snprintf(error, sizeof(error), "audio: no file access to open '%s'", path);
        else if (!file_native_open(files, path, &file))
            snprintf(error, sizeof(error), "audio: cannot open '%s': %s", path, file_get_error(files));
        else
            decoder = audio_stream_decoder_open_file(&file, rate, channels, error, sizeof(error));
        JS_FreeCString(ctx, path);
    }
    else
    {
        size_t size = 0;
        const uint8_t *bytes = js_audio_bytes_arg(ctx, argv[0], &size);
        if (!bytes)
            return JS_ThrowTypeError(ctx, "audio.openDecoder: source must be a path, an ArrayBuffer, or a typed array");
        decoder = audio_stream_decoder_open_memory(bytes, size, rate, channels, error, sizeof(error));
    }
    if (!decoder)
    {
        js_audio_fail(state, error[0] ? error : "audio: cannot open the decoder");
        return JS_NewInt32(ctx, -1);
    }
    return js_audio_store_decoder(state, ctx, decoder);
}

JS_AUDIO_CALLBACK(js_audio_create_decoder)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    if (!state)
        return JS_NewInt32(ctx, -1);
    state->error[0] = '\0';
    JSValueConst options = argc >= 1 ? argv[0] : JS_UNDEFINED;
    int rate = (int)js_audio_number_option(ctx, options, "sampleRate", 0);
    int channels = (int)js_audio_number_option(ctx, options, "channels", 2);
    if (channels != 1 && channels != 2)
        return JS_ThrowRangeError(ctx, "audio.createDecoder: channels must be 1 or 2");
    if (rate < 0 || rate > 384000)
        return JS_ThrowRangeError(ctx, "audio.createDecoder: sampleRate must be 0 (the data's) to 384000");
    return js_audio_store_decoder(state, ctx, audio_stream_decoder_create_push(rate, channels));
}

JS_AUDIO_CALLBACK(js_audio_feed_decoder)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    AudioStreamDecoder *decoder = argc >= 1 ? js_audio_decoder_arg(state, ctx, argv[0], NULL) : NULL;
    if (!decoder)
        return JS_ThrowTypeError(ctx, "audio.feedDecoder: not an open decoder");
    size_t size = 0;
    const uint8_t *bytes = NULL;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
    {
        bytes = js_audio_bytes_arg(ctx, argv[1], &size);
        if (!bytes)
            return JS_ThrowTypeError(ctx, "audio.feedDecoder: bytes must be an ArrayBuffer or a typed array");
    }
    bool end = argc >= 3 && JS_ToBool(ctx, argv[2]) > 0;
    char error[512] = "";
    state->error[0] = '\0';
    bool ok = audio_stream_decoder_feed(decoder, bytes, size, end, error, sizeof(error));
    if (!ok)
        js_audio_fail(state, error);
    return JS_NewBool(ctx, ok);
}

JS_AUDIO_CALLBACK(js_audio_decode)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    AudioStreamDecoder *decoder = argc >= 1 ? js_audio_decoder_arg(state, ctx, argv[0], NULL) : NULL;
    if (!decoder)
        return JS_ThrowTypeError(ctx, "audio.decode: not an open decoder");
    size_t count = 0;
    float *out = argc >= 2 ? js_audio_float32_arg(ctx, argv[1], &count) : NULL;
    if (!out)
        return JS_ThrowTypeError(ctx, "audio.decode(decoder, out): out must be a Float32Array");
    AudioStreamDecoderInfo info;
    audio_stream_decoder_info(decoder, &info);
    int channels = info.channels > 0 ? info.channels : 2;
    int frames = (int)(count / (size_t)channels);
    char error[512] = "";
    state->error[0] = '\0';
    int decoded = audio_stream_decoder_read(decoder, out, frames, error, sizeof(error));
    if (decoded < 0)
    {
        js_audio_fail(state, error[0] ? error : "audio: decoding failed");
        return JS_NewInt32(ctx, -1);
    }
    return JS_NewInt32(ctx, decoded);
}

JS_AUDIO_CALLBACK(js_audio_get_decoder_info)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    AudioStreamDecoder *decoder = argc >= 1 ? js_audio_decoder_arg(state, ctx, argv[0], NULL) : NULL;
    if (!decoder)
        return JS_ThrowTypeError(ctx, "audio.getDecoderInfo: not an open decoder");
    AudioStreamDecoderInfo info;
    audio_stream_decoder_info(decoder, &info);
    double rate = info.sample_rate > 0 ? (double)info.sample_rate : 0.0;
    JSValue result = JS_NewObject(ctx);
    js_audio_set_int(ctx, result, "sampleRate", info.sample_rate);
    js_audio_set_int(ctx, result, "channels", info.channels);
    js_audio_set_int(ctx, result, "sourceSampleRate", info.source_sample_rate);
    js_audio_set_int(ctx, result, "sourceChannels", info.source_channels);
    JS_SetPropertyStr(ctx, result, "duration",
                      JS_NewFloat64(ctx, info.length_frames >= 0 && rate > 0 ? (double)info.length_frames / rate : -1));
    JS_SetPropertyStr(ctx, result, "position",
                      JS_NewFloat64(ctx, rate > 0 ? (double)info.position_frames / rate : 0));
    JS_SetPropertyStr(ctx, result, "ready", JS_NewBool(ctx, info.ready));
    JS_SetPropertyStr(ctx, result, "ended", JS_NewBool(ctx, info.ended));
    JS_SetPropertyStr(ctx, result, "needsData", JS_NewBool(ctx, info.needs_data));
    JS_SetPropertyStr(ctx, result, "seekable", JS_NewBool(ctx, info.seekable));
    return result;
}

JS_AUDIO_CALLBACK(js_audio_seek_decoder)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    AudioStreamDecoder *decoder = argc >= 1 ? js_audio_decoder_arg(state, ctx, argv[0], NULL) : NULL;
    if (!decoder)
        return JS_ThrowTypeError(ctx, "audio.seekDecoder: not an open decoder");
    double seconds = 0;
    if (argc < 2 || JS_ToFloat64(ctx, &seconds, argv[1]) < 0)
        return JS_EXCEPTION;
    AudioStreamDecoderInfo info;
    audio_stream_decoder_info(decoder, &info);
    char error[512] = "";
    state->error[0] = '\0';
    bool ok = audio_stream_decoder_seek(decoder, (int64_t)(seconds > 0 ? seconds * info.sample_rate : 0), error,
                                        sizeof(error));
    if (!ok)
        js_audio_fail(state, error);
    return JS_NewBool(ctx, ok);
}

JS_AUDIO_CALLBACK(js_audio_close_decoder)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    int slot = -1;
    AudioStreamDecoder *decoder = argc >= 1 ? js_audio_decoder_arg(state, ctx, argv[0], &slot) : NULL;
    if (decoder)
    {
        audio_stream_decoder_close(decoder);
        state->decoders[slot] = NULL;
    }
    return JS_UNDEFINED;
}

typedef struct JsAudioFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsAudioFunction;

JS_AUDIO_CALLBACK(js_audio_fft)
{
    (void)this_val, (void)magic, (void)func_data;
    size_t count = 0, im_count = 0;
    float *re = argc >= 1 ? js_audio_float32_arg(ctx, argv[0], &count) : NULL;
    float *im = argc >= 2 ? js_audio_float32_arg(ctx, argv[1], &im_count) : NULL;
    if (!re || !im)
        return JS_ThrowTypeError(ctx, "audio.fft(re, im, inverse): re and im must be Float32Arrays");
    if (count != im_count || !audio_dsp_is_power_of_two(count) || count > AUDIO_DSP_MAX_SIZE)
        return JS_ThrowRangeError(ctx, "audio.fft: re and im must have the same power-of-two length (2 to 2^20)");
    bool inverse = argc >= 3 && JS_ToBool(ctx, argv[2]) > 0;
    if (!audio_dsp_fft(re, im, count, inverse))
        return JS_ThrowInternalError(ctx, "audio.fft: out of memory");
    return JS_TRUE;
}

JS_AUDIO_CALLBACK(js_audio_get_spectrum)
{
    (void)this_val, (void)magic, (void)func_data;
    size_t count = 0, out_count = 0;
    float *samples = argc >= 1 ? js_audio_float32_arg(ctx, argv[0], &count) : NULL;
    float *out = argc >= 2 ? js_audio_float32_arg(ctx, argv[1], &out_count) : NULL;
    if (!samples || !out)
        return JS_ThrowTypeError(ctx, "audio.getSpectrum(samples, outDb): both must be Float32Arrays");
    if (count < 4 || !audio_dsp_is_power_of_two(count) || count > AUDIO_DSP_MAX_SIZE)
        return JS_ThrowRangeError(ctx, "audio.getSpectrum: samples.length must be a power of two (4 to 2^20)");
    if (out_count < count / 2)
        return JS_ThrowRangeError(ctx, "audio.getSpectrum: outDb needs samples.length / 2 values");
    if (!audio_dsp_spectrum_db(samples, count, out))
        return JS_ThrowInternalError(ctx, "audio.getSpectrum: out of memory");
    return JS_DupValue(ctx, argv[1]);
}

JS_AUDIO_CALLBACK(js_audio_detect_pitch)
{
    (void)this_val, (void)magic;
    JsAudioContext *state = js_audio_binding_state(ctx, func_data);
    size_t count = 0;
    float *samples = argc >= 1 ? js_audio_float32_arg(ctx, argv[0], &count) : NULL;
    if (!samples)
        return JS_ThrowTypeError(ctx, "audio.detectPitch(samples, sampleRate, options): samples must be a Float32Array");
    if (count < 16 || count > AUDIO_DSP_MAX_SIZE / 2)
        return JS_ThrowRangeError(ctx, "audio.detectPitch: samples.length must be 16 to 2^19");
    double rate = 0;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) && JS_ToFloat64(ctx, &rate, argv[1]) < 0)
        return JS_EXCEPTION;
    if (!(rate > 0))
        rate = state && state->audio_ctx ? audio_stream_sample_rate(state->audio_ctx) : 48000;
    if (!(rate > 0))
        rate = 48000;
    AudioPitchOptions options;
    audio_dsp_pitch_defaults(&options);
    if (argc >= 3)
    {
        options.min_frequency = js_audio_number_option(ctx, argv[2], "minFrequency", options.min_frequency);
        options.max_frequency = js_audio_number_option(ctx, argv[2], "maxFrequency", options.max_frequency);
        options.min_clarity = js_audio_number_option(ctx, argv[2], "minClarity", options.min_clarity);
        options.min_level_db = js_audio_number_option(ctx, argv[2], "minLevel", options.min_level_db);
    }
    AudioPitch pitch;
    if (!audio_dsp_detect_pitch(samples, count, rate, &options, &pitch))
        return JS_ThrowInternalError(ctx, "audio.detectPitch: out of memory");
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "frequency", JS_NewFloat64(ctx, pitch.frequency));
    JS_SetPropertyStr(ctx, result, "clarity", JS_NewFloat64(ctx, pitch.clarity));
    JS_SetPropertyStr(ctx, result, "level", JS_NewFloat64(ctx, pitch.level_db));
    return result;
}

static const JsAudioFunction js_audio_funcs[] = {
    
    {"start", 0, js_audio_start},
    {"stop", 0, js_audio_stop},
    {"isPlaying", 0, js_audio_is_playing},
    {"setMasterGain", 1, js_audio_set_master_gain},
    {"getMasterGain", 0, js_audio_get_master_gain},

    {"createOscillator", 0, js_audio_create_oscillator},
    {"destroyOscillator", 1, js_audio_destroy_oscillator},
    {"setOscillatorType", 2, js_audio_oscillator_set_type},
    {"setOscillatorFrequency", 2, js_audio_oscillator_set_frequency},
    {"setOscillatorGain", 2, js_audio_oscillator_set_gain},
    {"setOscillatorDetune", 2, js_audio_oscillator_set_detune},
    {"startOscillator", 1, js_audio_oscillator_start},
    {"stopOscillator", 1, js_audio_oscillator_stop},
    {"setOscillatorEnvelope", 5, js_audio_oscillator_set_envelope},
    {"noteOn", 1, js_audio_oscillator_note_on},
    {"noteOff", 1, js_audio_oscillator_note_off},

    {"loadBuffer", 1, js_audio_load_buffer},
    {"loadBufferFromBuffer", 1, js_audio_load_buffer_from_buffer},
    {"createBuffer", 3, js_audio_create_buffer},
    {"setBufferData", 3, js_audio_buffer_set_data},
    {"destroyBuffer", 1, js_audio_destroy_buffer},
    {"playBuffer", 3, js_audio_play_buffer},
    {"stopBuffer", 1, js_audio_stop_buffer},

    {"midiToFreq", 1, js_audio_midi_to_freq},
    {"getError", 0, js_audio_get_error},

    {"getSampleRate", 0, js_audio_get_sample_rate},
    {"openOutput", 2, js_audio_open_output},
    {"closeOutput", 1, js_audio_close_output},
    {"openInput", 2, js_audio_open_input},
    {"closeInput", 1, js_audio_close_input},
    {"openDecoder", 2, js_audio_open_decoder},
    {"createDecoder", 1, js_audio_create_decoder},
    {"feedDecoder", 3, js_audio_feed_decoder},
    {"decode", 2, js_audio_decode},
    {"getDecoderInfo", 1, js_audio_get_decoder_info},
    {"seekDecoder", 2, js_audio_seek_decoder},
    {"closeDecoder", 1, js_audio_close_decoder},
    
    {"fft", 3, js_audio_fft},
    {"getSpectrum", 2, js_audio_get_spectrum},
    {"detectPitch", 3, js_audio_detect_pitch},
};

static int js_audio_add_function(JSContext *ctx, JSValue audio_obj,
                                 const JsAudioFunction *definition,
                                 JsAudioContext *state)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&state,
                                         sizeof(state));
    if (JS_IsException(data))
        return -1;

    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;

    return JS_SetPropertyStr(ctx, audio_obj, definition->name, function);
}

static void js_audio_add_constants(JSContext *ctx, JSValue audio_obj)
{
    JS_SetPropertyStr(ctx, audio_obj, "SINE", JS_NewInt32(ctx, AUDIO_WAVE_SINE));
    JS_SetPropertyStr(ctx, audio_obj, "SQUARE", JS_NewInt32(ctx, AUDIO_WAVE_SQUARE));
    JS_SetPropertyStr(ctx, audio_obj, "SAWTOOTH", JS_NewInt32(ctx, AUDIO_WAVE_SAWTOOTH));
    JS_SetPropertyStr(ctx, audio_obj, "TRIANGLE", JS_NewInt32(ctx, AUDIO_WAVE_TRIANGLE));
    JS_SetPropertyStr(ctx, audio_obj, "NOISE", JS_NewInt32(ctx, AUDIO_WAVE_NOISE));
}

JsAudioContext *js_audio_init(JSContext *ctx, const char *asset_root)
{
    JsAudioContext *state =
        (JsAudioContext *)calloc(1, sizeof(JsAudioContext));
    if (!state)
        return NULL;
    snprintf(state->asset_root, sizeof(state->asset_root), "%s",
             asset_root ? asset_root : ".");
    state->js = ctx;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");

    JSValue audio_obj = JS_NewObject(ctx);
    for (size_t index = 0; index < sizeof(js_audio_funcs) / sizeof(js_audio_funcs[0]); index++)
    {
        if (js_audio_add_function(ctx, audio_obj, &js_audio_funcs[index], state) < 0)
        {
            JS_FreeValue(ctx, audio_obj);
            JS_FreeValue(ctx, sys_obj);
            JS_FreeValue(ctx, global);
            free(state);
            return NULL;
        }
    }

    js_audio_add_constants(ctx, audio_obj);

    JS_SetPropertyStr(ctx, sys_obj, "audio", audio_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    return state;
}

void js_audio_set_files(JsAudioContext *state, FileContext *const *files)
{
    if (state)
        state->files = files;
}

void js_audio_cleanup(JsAudioContext *state)
{
    if (!state)
        return;
    for (int i = 0; i < AUDIO_MAX_OUTPUT_STREAMS; i++)
        js_audio_stream_release(state->js, &state->outputs[i]);
    for (int i = 0; i < AUDIO_MAX_INPUT_STREAMS; i++)
        js_audio_stream_release(state->js, &state->inputs[i]);
    for (int i = 0; i < JS_AUDIO_MAX_DECODERS; i++)
        audio_stream_decoder_close(state->decoders[i]);
    audio_destroy(state->audio_ctx);
    free(state);
}

#undef JS_AUDIO_CONTEXT
#undef JS_AUDIO_CALLBACK