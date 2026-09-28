#include "audio_wrapper.h"
#include "audio_decoder.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(__ANDROID__)
#define AUDIO_PLATFORM_ANDROID 1
#else
#define AUDIO_PLATFORM_SDL2 1
#include <SDL2/SDL.h>
#endif

static bool audio_path_is_absolute(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}

static bool audio_path_is_safe_relative(const char *path)
{
    if (!path || path[0] == '\0' || audio_path_is_absolute(path))
        return false;

    const char *segment = path;
    while (*segment)
    {
        const char *next = segment;
        while (*next && *next != '/' && *next != '\\')
            next++;

        size_t len = (size_t)(next - segment);
        if (len == 0 || (len == 1 && segment[0] == '.') ||
            (len == 2 && segment[0] == '.' && segment[1] == '.'))
            return false;

        segment = *next ? next + 1 : next;
    }

    return true;
}

static bool audio_resolve_asset_path(const char *asset_root, const char *path,
                                     char *out, size_t out_size,
                                     char *error, size_t error_size)
{
    if (!audio_path_is_safe_relative(path))
    {
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: unsafe asset path: %s", path ? path : "(null)");
        return false;
    }

    const char *root = (asset_root && asset_root[0]) ? asset_root : ".";
    int written = snprintf(out, out_size, "%s/%s", root, path);
    if (written < 0 || (size_t)written >= out_size)
    {
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: asset path is too long");
        return false;
    }

    return true;
}

static uint8_t *audio_read_asset_file(const char *asset_root, const char *path,
                                      size_t *out_size, char *error, size_t error_size)
{
    char resolved[4096];
    FILE *file;
    long size;
    uint8_t *data;

    if (out_size)
        *out_size = 0;

    if (!audio_resolve_asset_path(asset_root, path, resolved, sizeof(resolved), error, error_size))
        return NULL;

    file = fopen(resolved, "rb");
    if (!file)
    {
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: failed to open '%s'", path ? path : "(null)");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: failed to read file size: %s", path ? path : "(null)");
        return NULL;
    }
    size = ftell(file);
    if (size <= 0)
    {
        fclose(file);
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: decoded file is empty or too large: %s", path ? path : "(null)");
        return NULL;
    }
    rewind(file);

    data = (uint8_t *)malloc((size_t)size);
    if (!data)
    {
        fclose(file);
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: out of memory reading: %s", path ? path : "(null)");
        return NULL;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size)
    {
        free(data);
        fclose(file);
        if (error && error_size > 0)
            snprintf(error, error_size, "audio: failed to read: %s", path ? path : "(null)");
        return NULL;
    }

    fclose(file);
    if (out_size)
        *out_size = (size_t)size;
    return data;
}

static int audio_load_decoded_memory_into_buffer(AudioContext *ctx, const uint8_t *data, size_t size,
                                                 char *error, size_t error_size)
{
    AudioDecodedData decoded;

    if (!audio_decode_memory(data, size, &decoded, error, (int)error_size))
        return -1;

    int buffer_id = audio_create_buffer(ctx, decoded.sample_rate, decoded.channels, decoded.frame_count);
    if (buffer_id >= 0)
        audio_buffer_set_data(ctx, buffer_id, decoded.samples, 0, decoded.frame_count * decoded.channels);
    else if (error && error_size > 0)
        snprintf(error, error_size, "audio: no free buffer slot for buffer");

    audio_decoded_data_free(&decoded);
    return buffer_id;
}

#if AUDIO_PLATFORM_ANDROID

#include "android_audio.h"

struct AudioContext
{
    AndroidAudioContext *android_ctx;
    char asset_root[4096];
    char error_msg[256];
};

AudioContext *audio_create(void)
{
    if (!android_audio_is_available())
    {
        fprintf(stderr, "Android audio not available\n");
        return NULL;
    }

    AudioContext *ctx = (AudioContext *)calloc(1, sizeof(AudioContext));
    if (!ctx)
        return NULL;

    ctx->android_ctx = android_audio_create();
    if (!ctx->android_ctx)
    {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void audio_destroy(AudioContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->android_ctx)
    {
        android_audio_context_destroy(ctx->android_ctx);
    }
    free(ctx);
}

void audio_set_asset_root(AudioContext *ctx, const char *asset_root)
{
    if (!ctx)
        return;
    snprintf(ctx->asset_root, sizeof(ctx->asset_root), "%s", asset_root ? asset_root : ".");
}

void audio_start(AudioContext *ctx)
{
    if (ctx && ctx->android_ctx)
        android_audio_start(ctx->android_ctx);
}

void audio_stop(AudioContext *ctx)
{
    if (ctx && ctx->android_ctx)
        android_audio_stop(ctx->android_ctx);
}

bool audio_is_playing(AudioContext *ctx)
{
    return ctx && ctx->android_ctx ? android_audio_is_playing(ctx->android_ctx) : false;
}

void audio_set_master_gain(AudioContext *ctx, float gain)
{
    if (ctx && ctx->android_ctx)
        android_audio_set_master_gain(ctx->android_ctx, gain);
}

float audio_get_master_gain(AudioContext *ctx)
{
    return ctx && ctx->android_ctx ? android_audio_get_master_gain(ctx->android_ctx) : 0.0f;
}

int audio_create_oscillator(AudioContext *ctx)
{
    return ctx && ctx->android_ctx ? android_audio_create_oscillator(ctx->android_ctx) : -1;
}

void audio_destroy_oscillator(AudioContext *ctx, int osc_id)
{
    if (ctx && ctx->android_ctx)
        android_audio_destroy_oscillator(ctx->android_ctx, osc_id);
}

void audio_oscillator_set_type(AudioContext *ctx, int osc_id, AudioWaveType type)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_set_type(ctx->android_ctx, osc_id, type);
}

void audio_oscillator_set_frequency(AudioContext *ctx, int osc_id, float freq)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_set_frequency(ctx->android_ctx, osc_id, freq);
}

void audio_oscillator_set_gain(AudioContext *ctx, int osc_id, float gain)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_set_gain(ctx->android_ctx, osc_id, gain);
}

void audio_oscillator_set_detune(AudioContext *ctx, int osc_id, float cents)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_set_detune(ctx->android_ctx, osc_id, cents);
}

void audio_oscillator_start(AudioContext *ctx, int osc_id)
{
    if (ctx && ctx->android_ctx)
    {
        if (!android_audio_is_playing(ctx->android_ctx))
            android_audio_start(ctx->android_ctx);
        android_audio_oscillator_start(ctx->android_ctx, osc_id);
    }
}

void audio_oscillator_stop(AudioContext *ctx, int osc_id)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_stop(ctx->android_ctx, osc_id);
}

void audio_oscillator_set_envelope(AudioContext *ctx, int osc_id,
                                   float attack, float decay, float sustain, float release)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_set_envelope(ctx->android_ctx, osc_id, attack, decay, sustain, release);
}

void audio_oscillator_note_on(AudioContext *ctx, int osc_id)
{
    if (ctx && ctx->android_ctx)
    {
        if (!android_audio_is_playing(ctx->android_ctx))
            android_audio_start(ctx->android_ctx);
        android_audio_oscillator_note_on(ctx->android_ctx, osc_id);
    }
}

void audio_oscillator_note_off(AudioContext *ctx, int osc_id)
{
    if (ctx && ctx->android_ctx)
        android_audio_oscillator_note_off(ctx->android_ctx, osc_id);
}

int audio_create_buffer(AudioContext *ctx, int sample_rate, int channels, int num_samples)
{
    return ctx && ctx->android_ctx ? android_audio_create_buffer(ctx->android_ctx, sample_rate, channels, num_samples) : -1;
}

void audio_buffer_set_data(AudioContext *ctx, int buffer_id, const float *samples, int offset, int count)
{
    if (ctx && ctx->android_ctx)
        android_audio_buffer_set_data(ctx->android_ctx, buffer_id, samples, offset, count);
}

void audio_destroy_buffer(AudioContext *ctx, int buffer_id)
{
    if (ctx && ctx->android_ctx)
        android_audio_destroy_buffer(ctx->android_ctx, buffer_id);
}

int audio_play_buffer(AudioContext *ctx, int buffer_id, bool loop, float gain)
{
    if (!ctx || !ctx->android_ctx)
        return -1;
    if (!android_audio_is_playing(ctx->android_ctx))
        android_audio_start(ctx->android_ctx);
    return android_audio_play_buffer(ctx->android_ctx, buffer_id, loop, gain);
}

void audio_stop_buffer(AudioContext *ctx, int playback_id)
{
    if (ctx && ctx->android_ctx)
        android_audio_stop_buffer(ctx->android_ctx, playback_id);
}

int audio_load_buffer(AudioContext *ctx, const char *path)
{
    size_t size = 0;
    uint8_t *data;
    int buffer_id;

    if (!ctx || !ctx->android_ctx)
        return -1;
    ctx->error_msg[0] = '\0';

    data = audio_read_asset_file(ctx->asset_root, path, &size, ctx->error_msg, sizeof(ctx->error_msg));
    if (!data)
        return -1;
    buffer_id = audio_load_buffer_from_memory(ctx, data, size);
    free(data);
    return buffer_id;
}

int audio_load_buffer_from_memory(AudioContext *ctx, const uint8_t *data, size_t size)
{
    if (!ctx || !ctx->android_ctx)
        return -1;
    ctx->error_msg[0] = '\0';
    return audio_load_decoded_memory_into_buffer(ctx, data, size,
                                                 ctx->error_msg, sizeof(ctx->error_msg));
}

float audio_midi_to_freq(int note)
{
    return android_audio_midi_to_freq(note);
}

const char *audio_get_error(AudioContext *ctx)
{
    return ctx && ctx->android_ctx ? android_audio_get_error(ctx->android_ctx) : "Invalid context";
}

#else 

typedef enum
{
    ENVELOPE_OFF,
    ENVELOPE_ATTACK,
    ENVELOPE_DECAY,
    ENVELOPE_SUSTAIN,
    ENVELOPE_RELEASE
} EnvelopeState;

typedef struct
{
    bool active;
    AudioWaveType type;
    float frequency;
    float gain;
    float detune; 
    float phase;
    bool playing;

    float attack;
    float decay;
    float sustain;
    float release;
    EnvelopeState env_state;
    float env_level;
    float env_time;

    uint32_t noise_state;
} Oscillator;

typedef struct
{
    bool active;
    int sample_rate;
    int channels;
    int num_samples;
    float *data;
} AudioBuffer;

typedef struct
{
    bool active;
    int buffer_id;
    float position;
    bool loop;
    float gain;
} BufferPlayback;

#define MAX_BUFFER_PLAYBACKS 32

struct AudioContext
{
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    bool playing;
    float master_gain;
    char asset_root[4096];

    Oscillator oscillators[AUDIO_MAX_OSCILLATORS];
    AudioBuffer buffers[AUDIO_MAX_BUFFERS];
    BufferPlayback playbacks[MAX_BUFFER_PLAYBACKS];

    char error_msg[256];
};

static float generate_oscillator_sample(Oscillator *osc, float sample_rate)
{
    if (!osc->playing && osc->env_state == ENVELOPE_OFF)
        return 0.0f;

    float freq = osc->frequency * powf(2.0f, osc->detune / 1200.0f);
    float phase_inc = freq / sample_rate;
    float sample = 0.0f;

    switch (osc->type)
    {
    case AUDIO_WAVE_SINE:
        sample = sinf(osc->phase * 2.0f * M_PI);
        break;

    case AUDIO_WAVE_SQUARE:
        sample = (osc->phase < 0.5f) ? 1.0f : -1.0f;
        break;

    case AUDIO_WAVE_SAWTOOTH:
        sample = 2.0f * osc->phase - 1.0f;
        break;

    case AUDIO_WAVE_TRIANGLE:
        sample = 4.0f * fabsf(osc->phase - 0.5f) - 1.0f;
        break;

    case AUDIO_WAVE_NOISE:
        
        osc->noise_state = osc->noise_state * 1103515245 + 12345;
        sample = ((float)(osc->noise_state >> 16) / 32768.0f) - 1.0f;
        break;
    }

    osc->phase += phase_inc;
    if (osc->phase >= 1.0f)
        osc->phase -= 1.0f;

    float env_delta = 1.0f / sample_rate;
    switch (osc->env_state)
    {
    case ENVELOPE_ATTACK:
        osc->env_time += env_delta;
        if (osc->attack > 0.0f)
        {
            osc->env_level = osc->env_time / osc->attack;
            if (osc->env_level >= 1.0f)
            {
                osc->env_level = 1.0f;
                osc->env_state = ENVELOPE_DECAY;
                osc->env_time = 0.0f;
            }
        }
        else
        {
            osc->env_level = 1.0f;
            osc->env_state = ENVELOPE_DECAY;
            osc->env_time = 0.0f;
        }
        break;

    case ENVELOPE_DECAY:
        osc->env_time += env_delta;
        if (osc->decay > 0.0f)
        {
            osc->env_level = 1.0f - (1.0f - osc->sustain) * (osc->env_time / osc->decay);
            if (osc->env_level <= osc->sustain)
            {
                osc->env_level = osc->sustain;
                osc->env_state = ENVELOPE_SUSTAIN;
            }
        }
        else
        {
            osc->env_level = osc->sustain;
            osc->env_state = ENVELOPE_SUSTAIN;
        }
        break;

    case ENVELOPE_SUSTAIN:
        osc->env_level = osc->sustain;
        break;

    case ENVELOPE_RELEASE:
        osc->env_time += env_delta;
        if (osc->release > 0.0f)
        {
            float release_start = osc->sustain;
            osc->env_level = release_start * (1.0f - osc->env_time / osc->release);
            if (osc->env_level <= 0.0f)
            {
                osc->env_level = 0.0f;
                osc->env_state = ENVELOPE_OFF;
                osc->playing = false;
            }
        }
        else
        {
            osc->env_level = 0.0f;
            osc->env_state = ENVELOPE_OFF;
            osc->playing = false;
        }
        break;

    case ENVELOPE_OFF:
    default:
        osc->env_level = osc->playing ? 1.0f : 0.0f;
        break;
    }

    return sample * osc->gain * osc->env_level;
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    AudioContext *ctx = (AudioContext *)userdata;
    float *output = (float *)stream;
    int num_samples = len / sizeof(float) / ctx->spec.channels;

    memset(stream, 0, len);

    for (int s = 0; s < num_samples; s++)
    {
        float left = 0.0f, right = 0.0f;

        for (int i = 0; i < AUDIO_MAX_OSCILLATORS; i++)
        {
            Oscillator *osc = &ctx->oscillators[i];
            if (osc->active && (osc->playing || osc->env_state != ENVELOPE_OFF))
            {
                float sample = generate_oscillator_sample(osc, (float)ctx->spec.freq);
                left += sample;
                right += sample;
            }
        }

        for (int i = 0; i < MAX_BUFFER_PLAYBACKS; i++)
        {
            BufferPlayback *pb = &ctx->playbacks[i];
            if (!pb->active)
                continue;

            AudioBuffer *buf = &ctx->buffers[pb->buffer_id];
            if (!buf->active)
            {
                pb->active = false;
                continue;
            }

            int sample_index = (int)pb->position;
            if (sample_index >= buf->num_samples)
            {
                if (pb->loop)
                {
                    pb->position = 0.0f;
                    sample_index = 0;
                }
                else
                {
                    pb->active = false;
                    continue;
                }
            }

            if (buf->channels == 1)
            {
                float sample = buf->data[sample_index] * pb->gain;
                left += sample;
                right += sample;
            }
            else
            {
                left += buf->data[sample_index * 2] * pb->gain;
                right += buf->data[sample_index * 2 + 1] * pb->gain;
            }

            pb->position += (float)buf->sample_rate / (float)ctx->spec.freq;
        }

        left *= ctx->master_gain;
        right *= ctx->master_gain;
        left = fmaxf(-1.0f, fminf(1.0f, left));
        right = fmaxf(-1.0f, fminf(1.0f, right));

        if (ctx->spec.channels == 2)
        {
            output[s * 2] = left;
            output[s * 2 + 1] = right;
        }
        else
        {
            output[s] = (left + right) * 0.5f;
        }
    }
}

AudioContext *audio_create(void)
{
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        {
            fprintf(stderr, "Failed to initialize SDL audio: %s\n", SDL_GetError());
            return NULL;
        }
    }

    AudioContext *ctx = (AudioContext *)calloc(1, sizeof(AudioContext));
    if (!ctx)
        return NULL;

    ctx->master_gain = 0.5f;

    SDL_AudioSpec wanted;
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = AUDIO_SAMPLE_RATE;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = AUDIO_CHANNELS;
    wanted.samples = 256; 
    wanted.callback = audio_callback;
    wanted.userdata = ctx;

    ctx->device = SDL_OpenAudioDevice(NULL, 0, &wanted, &ctx->spec, 0);
    if (ctx->device == 0)
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Failed to open audio device: %s", SDL_GetError());
        free(ctx);
        return NULL;
    }

    return ctx;
}

void audio_destroy(AudioContext *ctx)
{
    if (!ctx)
        return;

    SDL_CloseAudioDevice(ctx->device);

    for (int i = 0; i < AUDIO_MAX_BUFFERS; i++)
    {
        if (ctx->buffers[i].active && ctx->buffers[i].data)
        {
            free(ctx->buffers[i].data);
        }
    }

    free(ctx);
}

void audio_set_asset_root(AudioContext *ctx, const char *asset_root)
{
    if (!ctx)
        return;
    snprintf(ctx->asset_root, sizeof(ctx->asset_root), "%s", asset_root ? asset_root : ".");
}

void audio_start(AudioContext *ctx)
{
    if (!ctx)
        return;
    SDL_PauseAudioDevice(ctx->device, 0);
    ctx->playing = true;
}

void audio_stop(AudioContext *ctx)
{
    if (!ctx)
        return;
    SDL_PauseAudioDevice(ctx->device, 1);
    ctx->playing = false;
}

bool audio_is_playing(AudioContext *ctx)
{
    return ctx ? ctx->playing : false;
}

void audio_set_master_gain(AudioContext *ctx, float gain)
{
    if (!ctx)
        return;
    SDL_LockAudioDevice(ctx->device);
    ctx->master_gain = fmaxf(0.0f, fminf(1.0f, gain));
    SDL_UnlockAudioDevice(ctx->device);
}

float audio_get_master_gain(AudioContext *ctx)
{
    return ctx ? ctx->master_gain : 0.0f;
}

int audio_create_oscillator(AudioContext *ctx)
{
    if (!ctx)
        return -1;

    SDL_LockAudioDevice(ctx->device);
    for (int i = 0; i < AUDIO_MAX_OSCILLATORS; i++)
    {
        if (!ctx->oscillators[i].active)
        {
            memset(&ctx->oscillators[i], 0, sizeof(Oscillator));
            ctx->oscillators[i].active = true;
            ctx->oscillators[i].type = AUDIO_WAVE_SINE;
            ctx->oscillators[i].frequency = 440.0f;
            ctx->oscillators[i].gain = 0.5f;
            ctx->oscillators[i].sustain = 1.0f;
            ctx->oscillators[i].noise_state = 12345;
            SDL_UnlockAudioDevice(ctx->device);
            return i;
        }
    }
    SDL_UnlockAudioDevice(ctx->device);
    return -1;
}

void audio_destroy_oscillator(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].active = false;
    ctx->oscillators[osc_id].playing = false;
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_set_type(AudioContext *ctx, int osc_id, AudioWaveType type)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].type = type;
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_set_frequency(AudioContext *ctx, int osc_id, float freq)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].frequency = fmaxf(20.0f, fminf(20000.0f, freq));
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_set_gain(AudioContext *ctx, int osc_id, float gain)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].gain = fmaxf(0.0f, fminf(1.0f, gain));
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_start(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    if (!ctx->playing)
        audio_start(ctx);

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].playing = true;
    ctx->oscillators[osc_id].phase = 0.0f;
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_stop(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].playing = false;
    ctx->oscillators[osc_id].env_state = ENVELOPE_OFF;
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_set_detune(AudioContext *ctx, int osc_id, float cents)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].detune = cents;
    SDL_UnlockAudioDevice(ctx->device);
}

int audio_create_buffer(AudioContext *ctx, int sample_rate, int channels, int num_samples)
{
    if (!ctx || sample_rate <= 0 || channels < 1 || channels > 2 || num_samples <= 0)
        return -1;

    SDL_LockAudioDevice(ctx->device);
    for (int i = 0; i < AUDIO_MAX_BUFFERS; i++)
    {
        if (!ctx->buffers[i].active)
        {
            ctx->buffers[i].sample_rate = sample_rate;
            ctx->buffers[i].channels = channels;
            ctx->buffers[i].num_samples = num_samples;
            ctx->buffers[i].data = (float *)calloc(num_samples * channels, sizeof(float));
            if (!ctx->buffers[i].data)
            {
                SDL_UnlockAudioDevice(ctx->device);
                return -1;
            }
            ctx->buffers[i].active = true;
            SDL_UnlockAudioDevice(ctx->device);
            return i;
        }
    }
    SDL_UnlockAudioDevice(ctx->device);
    return -1;
}

void audio_buffer_set_data(AudioContext *ctx, int buffer_id, const float *samples, int offset, int count)
{
    if (!ctx || buffer_id < 0 || buffer_id >= AUDIO_MAX_BUFFERS)
        return;
    AudioBuffer *buf = &ctx->buffers[buffer_id];
    if (!buf->active || !samples)
        return;

    int total = buf->num_samples * buf->channels;
    if (offset < 0 || offset >= total)
        return;
    if (offset + count > total)
        count = total - offset;

    SDL_LockAudioDevice(ctx->device);
    memcpy(buf->data + offset, samples, count * sizeof(float));
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_destroy_buffer(AudioContext *ctx, int buffer_id)
{
    if (!ctx || buffer_id < 0 || buffer_id >= AUDIO_MAX_BUFFERS)
        return;

    SDL_LockAudioDevice(ctx->device);

    for (int i = 0; i < MAX_BUFFER_PLAYBACKS; i++)
    {
        if (ctx->playbacks[i].active && ctx->playbacks[i].buffer_id == buffer_id)
        {
            ctx->playbacks[i].active = false;
        }
    }

    if (ctx->buffers[buffer_id].data)
    {
        free(ctx->buffers[buffer_id].data);
        ctx->buffers[buffer_id].data = NULL;
    }
    ctx->buffers[buffer_id].active = false;

    SDL_UnlockAudioDevice(ctx->device);
}

int audio_play_buffer(AudioContext *ctx, int buffer_id, bool loop, float gain)
{
    if (!ctx || buffer_id < 0 || buffer_id >= AUDIO_MAX_BUFFERS)
        return -1;
    if (!ctx->buffers[buffer_id].active)
        return -1;

    if (!ctx->playing)
        audio_start(ctx);

    SDL_LockAudioDevice(ctx->device);
    for (int i = 0; i < MAX_BUFFER_PLAYBACKS; i++)
    {
        if (!ctx->playbacks[i].active)
        {
            ctx->playbacks[i].active = true;
            ctx->playbacks[i].buffer_id = buffer_id;
            ctx->playbacks[i].position = 0.0f;
            ctx->playbacks[i].loop = loop;
            ctx->playbacks[i].gain = fmaxf(0.0f, fminf(1.0f, gain));
            SDL_UnlockAudioDevice(ctx->device);
            return i;
        }
    }
    SDL_UnlockAudioDevice(ctx->device);
    return -1;
}

void audio_stop_buffer(AudioContext *ctx, int playback_id)
{
    if (!ctx || playback_id < 0 || playback_id >= MAX_BUFFER_PLAYBACKS)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->playbacks[playback_id].active = false;
    SDL_UnlockAudioDevice(ctx->device);
}

int audio_load_buffer(AudioContext *ctx, const char *path)
{
    size_t size = 0;
    uint8_t *data;
    int buffer_id;

    if (!ctx)
        return -1;
    ctx->error_msg[0] = '\0';

    data = audio_read_asset_file(ctx->asset_root, path, &size, ctx->error_msg, sizeof(ctx->error_msg));
    if (!data)
        return -1;
    buffer_id = audio_load_buffer_from_memory(ctx, data, size);
    free(data);
    return buffer_id;
}

int audio_load_buffer_from_memory(AudioContext *ctx, const uint8_t *data, size_t size)
{
    if (!ctx)
        return -1;
    ctx->error_msg[0] = '\0';
    return audio_load_decoded_memory_into_buffer(ctx, data, size,
                                                 ctx->error_msg, sizeof(ctx->error_msg));
}

void audio_oscillator_set_envelope(AudioContext *ctx, int osc_id,
                                   float attack, float decay, float sustain, float release)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].attack = fmaxf(0.0f, attack);
    ctx->oscillators[osc_id].decay = fmaxf(0.0f, decay);
    ctx->oscillators[osc_id].sustain = fmaxf(0.0f, fminf(1.0f, sustain));
    ctx->oscillators[osc_id].release = fmaxf(0.0f, release);
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_note_on(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    if (!ctx->playing)
        audio_start(ctx);

    SDL_LockAudioDevice(ctx->device);
    ctx->oscillators[osc_id].playing = true;
    ctx->oscillators[osc_id].env_state = ENVELOPE_ATTACK;
    ctx->oscillators[osc_id].env_time = 0.0f;
    ctx->oscillators[osc_id].env_level = 0.0f;
    SDL_UnlockAudioDevice(ctx->device);
}

void audio_oscillator_note_off(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    if (!ctx->oscillators[osc_id].active)
        return;

    SDL_LockAudioDevice(ctx->device);
    if (ctx->oscillators[osc_id].env_state != ENVELOPE_OFF)
    {
        ctx->oscillators[osc_id].env_state = ENVELOPE_RELEASE;
        ctx->oscillators[osc_id].env_time = 0.0f;
    }
    SDL_UnlockAudioDevice(ctx->device);
}

float audio_midi_to_freq(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

const char *audio_get_error(AudioContext *ctx)
{
    return ctx ? ctx->error_msg : "Invalid context";
}

#endif