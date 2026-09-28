#include "tests/audio_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AudioContext
{
    bool playing;
    float master_gain;
    char asset_root[4096];
    char error[128];
};

static int create_count;
static int destroy_count;
static int live_count;

void audio_mock_reset(void)
{
    create_count = 0;
    destroy_count = 0;
    live_count = 0;
}

int audio_mock_create_count(void)
{
    return create_count;
}

int audio_mock_destroy_count(void)
{
    return destroy_count;
}

int audio_mock_live_count(void)
{
    return live_count;
}

int audio_mock_asset_root_id(const char *asset_root)
{
    const unsigned char *cursor =
        (const unsigned char *)(asset_root ? asset_root : ".");
    unsigned int hash = 2166136261u;

    while (*cursor)
    {
        hash ^= *cursor++;
        hash *= 16777619u;
    }
    return (int)((hash & 0x3fffffffu) + 1u);
}

AudioContext *audio_create(void)
{
    AudioContext *ctx = (AudioContext *)calloc(1, sizeof(AudioContext));
    if (!ctx)
        return NULL;
    ctx->master_gain = 0.5f;
    create_count++;
    live_count++;
    return ctx;
}

void audio_destroy(AudioContext *ctx)
{
    if (!ctx)
        return;
    destroy_count++;
    live_count--;
    free(ctx);
}

void audio_set_asset_root(AudioContext *ctx, const char *asset_root)
{
    if (ctx)
        snprintf(ctx->asset_root, sizeof(ctx->asset_root), "%s",
                 asset_root ? asset_root : ".");
}

void audio_start(AudioContext *ctx)
{
    if (ctx)
        ctx->playing = true;
}

void audio_stop(AudioContext *ctx)
{
    if (ctx)
        ctx->playing = false;
}

bool audio_is_playing(AudioContext *ctx)
{
    return ctx && ctx->playing;
}

void audio_set_master_gain(AudioContext *ctx, float gain)
{
    if (ctx)
        ctx->master_gain = gain;
}

float audio_get_master_gain(AudioContext *ctx)
{
    return ctx ? ctx->master_gain : 0.0f;
}

int audio_create_oscillator(AudioContext *ctx)
{
    return ctx ? audio_mock_asset_root_id(ctx->asset_root) : -1;
}

void audio_destroy_oscillator(AudioContext *ctx, int osc_id)
{
    (void)ctx;
    (void)osc_id;
}

void audio_oscillator_set_type(AudioContext *ctx, int osc_id, AudioWaveType type)
{
    (void)ctx;
    (void)osc_id;
    (void)type;
}

void audio_oscillator_set_frequency(AudioContext *ctx, int osc_id, float freq)
{
    (void)ctx;
    (void)osc_id;
    (void)freq;
}

void audio_oscillator_set_gain(AudioContext *ctx, int osc_id, float gain)
{
    (void)ctx;
    (void)osc_id;
    (void)gain;
}

void audio_oscillator_start(AudioContext *ctx, int osc_id)
{
    (void)osc_id;
    audio_start(ctx);
}

void audio_oscillator_stop(AudioContext *ctx, int osc_id)
{
    (void)ctx;
    (void)osc_id;
}

void audio_oscillator_set_detune(AudioContext *ctx, int osc_id, float cents)
{
    (void)ctx;
    (void)osc_id;
    (void)cents;
}

int audio_create_buffer(AudioContext *ctx, int sample_rate, int channels,
                        int num_samples)
{
    (void)sample_rate;
    (void)channels;
    (void)num_samples;
    return ctx ? 0 : -1;
}

void audio_buffer_set_data(AudioContext *ctx, int buffer_id,
                           const float *samples, int offset, int count)
{
    (void)ctx;
    (void)buffer_id;
    (void)samples;
    (void)offset;
    (void)count;
}

void audio_destroy_buffer(AudioContext *ctx, int buffer_id)
{
    (void)ctx;
    (void)buffer_id;
}

int audio_play_buffer(AudioContext *ctx, int buffer_id, bool loop, float gain)
{
    (void)buffer_id;
    (void)loop;
    (void)gain;
    return ctx ? 0 : -1;
}

void audio_stop_buffer(AudioContext *ctx, int playback_id)
{
    (void)ctx;
    (void)playback_id;
}

int audio_load_buffer(AudioContext *ctx, const char *path)
{
    (void)path;
    return ctx ? audio_mock_asset_root_id(ctx->asset_root) : -1;
}

int audio_load_buffer_from_memory(AudioContext *ctx, const uint8_t *data,
                                  size_t size)
{
    (void)data;
    (void)size;
    return ctx ? 0 : -1;
}

void audio_oscillator_set_envelope(AudioContext *ctx, int osc_id,
                                   float attack, float decay, float sustain,
                                   float release)
{
    (void)ctx;
    (void)osc_id;
    (void)attack;
    (void)decay;
    (void)sustain;
    (void)release;
}

void audio_oscillator_note_on(AudioContext *ctx, int osc_id)
{
    (void)osc_id;
    audio_start(ctx);
}

void audio_oscillator_note_off(AudioContext *ctx, int osc_id)
{
    (void)ctx;
    (void)osc_id;
}

float audio_midi_to_freq(int note)
{
    return (float)note;
}

const char *audio_get_error(AudioContext *ctx)
{
    return ctx ? ctx->error : "Invalid context";
}