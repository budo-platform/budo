#ifndef BUDO_AUDIO_RING_H
#define BUDO_AUDIO_RING_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline uint64_t audio_ring_load(volatile uint64_t *value)
{
    return (uint64_t)_InterlockedCompareExchange64((volatile long long *)value, 0, 0);
}
static inline void audio_ring_store(volatile uint64_t *value, uint64_t next)
{
    _InterlockedExchange64((volatile long long *)value, (long long)next);
}
#else
static inline uint64_t audio_ring_load(volatile uint64_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}
static inline void audio_ring_store(volatile uint64_t *value, uint64_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}
#endif

typedef struct AudioRing
{
    float *data;
    int capacity; 
    int channels;
    volatile uint64_t written; 
    volatile uint64_t read;    
} AudioRing;

static inline bool audio_ring_init(AudioRing *ring, int capacity_frames, int channels)
{
    memset(ring, 0, sizeof(*ring));
    if (capacity_frames <= 0 || channels <= 0)
        return false;
    ring->data = (float *)calloc((size_t)capacity_frames * (size_t)channels, sizeof(float));
    if (!ring->data)
        return false;
    ring->capacity = capacity_frames;
    ring->channels = channels;
    return true;
}

static inline void audio_ring_free(AudioRing *ring)
{
    free(ring->data);
    memset(ring, 0, sizeof(*ring));
}

static inline int audio_ring_queued(AudioRing *ring)
{
    uint64_t written = audio_ring_load(&ring->written);
    uint64_t read = audio_ring_load(&ring->read);
    return (int)(written - read);
}

static inline int audio_ring_write(AudioRing *ring, const float *frames_in, int frames)
{
    uint64_t written = ring->written;
    uint64_t read = audio_ring_load(&ring->read);
    int space = ring->capacity - (int)(written - read);
    if (frames > space)
        frames = space;
    for (int done = 0; done < frames;)
    {
        int at = (int)((written + (uint64_t)done) % (uint64_t)ring->capacity);
        int run = ring->capacity - at;
        if (run > frames - done)
            run = frames - done;
        memcpy(ring->data + (size_t)at * (size_t)ring->channels,
               frames_in + (size_t)done * (size_t)ring->channels,
               (size_t)run * (size_t)ring->channels * sizeof(float));
        done += run;
    }
    audio_ring_store(&ring->written, written + (uint64_t)frames);
    return frames;
}

static inline int audio_ring_read(AudioRing *ring, float *frames_out, int frames)
{
    uint64_t read = ring->read;
    uint64_t written = audio_ring_load(&ring->written);
    int queued = (int)(written - read);
    if (frames > queued)
        frames = queued;
    for (int done = 0; done < frames;)
    {
        int at = (int)((read + (uint64_t)done) % (uint64_t)ring->capacity);
        int run = ring->capacity - at;
        if (run > frames - done)
            run = frames - done;
        memcpy(frames_out + (size_t)done * (size_t)ring->channels,
               ring->data + (size_t)at * (size_t)ring->channels,
               (size_t)run * (size_t)ring->channels * sizeof(float));
        done += run;
    }
    audio_ring_store(&ring->read, read + (uint64_t)frames);
    return frames;
}

#endif