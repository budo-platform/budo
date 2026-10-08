#ifndef BUDO_AUDIO_STREAMS_H
#define BUDO_AUDIO_STREAMS_H

#include "audio_ring.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define AUDIO_MAX_OUTPUT_STREAMS 8
#define AUDIO_MAX_INPUT_STREAMS 4

#define AUDIO_STREAM_CHUNK_FRAMES 256

#define AUDIO_STREAM_ADAPTIVE_START_MS 20.0
#define AUDIO_STREAM_ADAPTIVE_MAX_MS 250.0
#define AUDIO_STREAM_ADAPTIVE_GROW 2
#define AUDIO_STREAM_ADAPTIVE_WINDOW_SECONDS 3
#define AUDIO_STREAM_ADAPTIVE_WARMUP_SECONDS 0.5

#define AUDIO_STREAM_ADAPTIVE_STABLE_SECONDS 8

    typedef struct AudioStreamStats
    {
        int sample_rate;
        int channels;
        int latency_frames; 
        int queued_frames;
        uint64_t frames;    
        uint64_t glitches;  
    } AudioStreamStats;

    typedef struct AudioStreamSlot
    {
        bool active;
        AudioRing ring;
        volatile int target_frames; 
        
        bool adaptive;
        int min_target_frames;
        int max_target_frames;
        uint64_t seen_glitches;
        uint64_t steady_since;      
        volatile uint64_t low_water; 
        volatile uint64_t missing;   
        bool starving;               
        uint64_t seen_missing;
        volatile uint64_t frames;   
        volatile uint64_t glitches; 
        volatile uint64_t primed;   
    } AudioStreamSlot;

    typedef struct AudioStreams
    {
        int sample_rate;
        double adaptive_start_ms; 
        AudioStreamSlot outputs[AUDIO_MAX_OUTPUT_STREAMS];
        AudioStreamSlot inputs[AUDIO_MAX_INPUT_STREAMS];

        float *mix_scratch;
        float *capture_scratch;
        int scratch_frames;
        volatile uint64_t wake_pending;
    } AudioStreams;

    bool audio_streams_init(AudioStreams *streams, int sample_rate);
    void audio_streams_destroy(AudioStreams *streams);

    int audio_streams_open(AudioStreams *streams, bool input, int channels, double latency_ms);
    void audio_streams_close(AudioStreams *streams, bool input, int id);
    bool audio_streams_is_open(const AudioStreams *streams, bool input, int id);
    bool audio_streams_any_open(const AudioStreams *streams);

    int audio_streams_output_wanted(AudioStreams *streams, int id);
    int audio_streams_output_write(AudioStreams *streams, int id, const float *frames, int count);

    int audio_streams_input_available(AudioStreams *streams, int id);
    int audio_streams_input_read(AudioStreams *streams, int id, float *frames, int count);

    bool audio_streams_stats(AudioStreams *streams, bool input, int id, AudioStreamStats *out);

    void audio_streams_mix(AudioStreams *streams, float *out, int frames, int channels);
    
    void audio_streams_capture(AudioStreams *streams, const float *in, int frames, int channels);

    bool audio_streams_take_wake(AudioStreams *streams);
    
    void audio_streams_clear_wake(AudioStreams *streams);

#ifdef __cplusplus
}
#endif

#endif