#include "js_audio_bindings.h"
#include "audio_service.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef QUICKJS_NG
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(val)
#else
#define BUDO_JS_IS_ARRAY(ctx, val) JS_IsArray(ctx, val)
#endif

struct JsAudioContext
{
    AudioContext *audio_ctx;
    bool lazy_initialized;
    char asset_root[4096];
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

typedef struct JsAudioFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsAudioFunction;

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

void js_audio_cleanup(JsAudioContext *state)
{
    if (!state)
        return;
    audio_destroy(state->audio_ctx);
    free(state);
}

#undef JS_AUDIO_CONTEXT
#undef JS_AUDIO_CALLBACK