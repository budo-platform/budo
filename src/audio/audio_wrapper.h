#ifndef AUDIO_WRAPPER_H
#define AUDIO_WRAPPER_H

#include "audio_streams.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define AUDIO_MAX_OSCILLATORS 32
#define AUDIO_MAX_BUFFERS 64
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2

    typedef enum
    {
        AUDIO_WAVE_SINE,
        AUDIO_WAVE_SQUARE,
        AUDIO_WAVE_SAWTOOTH,
        AUDIO_WAVE_TRIANGLE,
        AUDIO_WAVE_NOISE
    } AudioWaveType;

    typedef struct AudioContext AudioContext;

    AudioContext *audio_create(void);

    void audio_destroy(AudioContext *ctx);

    void audio_set_asset_root(AudioContext *ctx, const char *asset_root);

    void audio_start(AudioContext *ctx);

    void audio_stop(AudioContext *ctx);

    bool audio_is_playing(AudioContext *ctx);

    void audio_set_master_gain(AudioContext *ctx, float gain);

    float audio_get_master_gain(AudioContext *ctx);

    int audio_create_oscillator(AudioContext *ctx);

    void audio_destroy_oscillator(AudioContext *ctx, int osc_id);

    void audio_oscillator_set_type(AudioContext *ctx, int osc_id, AudioWaveType type);

    void audio_oscillator_set_frequency(AudioContext *ctx, int osc_id, float freq);

    void audio_oscillator_set_gain(AudioContext *ctx, int osc_id, float gain);

    void audio_oscillator_start(AudioContext *ctx, int osc_id);

    void audio_oscillator_stop(AudioContext *ctx, int osc_id);

    void audio_oscillator_set_detune(AudioContext *ctx, int osc_id, float cents);

    int audio_create_buffer(AudioContext *ctx, int sample_rate, int channels, int num_samples);

    void audio_buffer_set_data(AudioContext *ctx, int buffer_id, const float *samples, int offset, int count);

    void audio_destroy_buffer(AudioContext *ctx, int buffer_id);

    int audio_play_buffer(AudioContext *ctx, int buffer_id, bool loop, float gain);

    void audio_stop_buffer(AudioContext *ctx, int playback_id);

    int audio_load_buffer(AudioContext *ctx, const char *path);

    int audio_load_buffer_from_memory(AudioContext *ctx, const uint8_t *data, size_t size);

    void audio_oscillator_set_envelope(AudioContext *ctx, int osc_id,
                                       float attack, float decay, float sustain, float release);

    void audio_oscillator_note_on(AudioContext *ctx, int osc_id);

    void audio_oscillator_note_off(AudioContext *ctx, int osc_id);

    float audio_midi_to_freq(int note);

    static inline bool audio_wave_type_from_string(const char *str, AudioWaveType *out_type)
    {
        if (!str || !out_type)
            return false;
        if (strcmp(str, "sine") == 0)
        {
            *out_type = AUDIO_WAVE_SINE;
            return true;
        }
        if (strcmp(str, "square") == 0)
        {
            *out_type = AUDIO_WAVE_SQUARE;
            return true;
        }
        if (strcmp(str, "sawtooth") == 0)
        {
            *out_type = AUDIO_WAVE_SAWTOOTH;
            return true;
        }
        if (strcmp(str, "triangle") == 0)
        {
            *out_type = AUDIO_WAVE_TRIANGLE;
            return true;
        }
        if (strcmp(str, "noise") == 0)
        {
            *out_type = AUDIO_WAVE_NOISE;
            return true;
        }
        return false;
    }

    const char *audio_get_error(AudioContext *ctx);

    int audio_stream_sample_rate(AudioContext *ctx);
    
    int audio_output_open(AudioContext *ctx, int channels, double latency_ms);
    void audio_output_close(AudioContext *ctx, int id);
    
    int audio_output_wanted(AudioContext *ctx, int id);
    int audio_output_write(AudioContext *ctx, int id, const float *frames, int count);
    
    int audio_input_open(AudioContext *ctx, int channels, double latency_ms);
    void audio_input_close(AudioContext *ctx, int id);
    int audio_input_available(AudioContext *ctx, int id);
    int audio_input_read(AudioContext *ctx, int id, float *frames, int count);
    bool audio_stream_get_stats(AudioContext *ctx, bool input, int id, AudioStreamStats *out);
    
    bool audio_streams_active(AudioContext *ctx);
    
    void audio_streams_serviced(AudioContext *ctx);

#ifdef __cplusplus
}
#endif

#endif