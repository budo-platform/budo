#include "audio_streams.h"

#include <math.h>

#define SCRATCH_FRAMES 4096

bool audio_streams_init(AudioStreams *streams, int sample_rate)
{
    memset(streams, 0, sizeof(*streams));
    streams->sample_rate = sample_rate > 0 ? sample_rate : 48000;
    streams->adaptive_start_ms = AUDIO_STREAM_ADAPTIVE_START_MS;
    streams->mix_scratch = (float *)calloc((size_t)SCRATCH_FRAMES * 2, sizeof(float));
    streams->capture_scratch = (float *)calloc((size_t)SCRATCH_FRAMES * 2, sizeof(float));
    if (!streams->mix_scratch || !streams->capture_scratch)
    {
        free(streams->mix_scratch);
        free(streams->capture_scratch);
        streams->mix_scratch = streams->capture_scratch = NULL;
        return false;
    }
    streams->scratch_frames = SCRATCH_FRAMES;
    return true;
}

void audio_streams_destroy(AudioStreams *streams)
{
    for (int i = 0; i < AUDIO_MAX_OUTPUT_STREAMS; i++)
        audio_streams_close(streams, false, i);
    for (int i = 0; i < AUDIO_MAX_INPUT_STREAMS; i++)
        audio_streams_close(streams, true, i);
    free(streams->mix_scratch);
    free(streams->capture_scratch);
    streams->mix_scratch = streams->capture_scratch = NULL;
    streams->scratch_frames = 0;
}

static AudioStreamSlot *slot_at(AudioStreams *streams, bool input, int id)
{
    int count = input ? AUDIO_MAX_INPUT_STREAMS : AUDIO_MAX_OUTPUT_STREAMS;
    if (!streams || id < 0 || id >= count)
        return NULL;
    AudioStreamSlot *slot = input ? &streams->inputs[id] : &streams->outputs[id];
    return slot->active ? slot : NULL;
}

int audio_streams_open(AudioStreams *streams, bool input, int channels, double latency_ms)
{
    if (!streams || channels < 1 || channels > 2)
        return -1;
    bool adaptive = !input && !(latency_ms > 0.0);
    if (!(latency_ms > 0.0))
        latency_ms = input ? 40.0 : streams->adaptive_start_ms;
    const int min_target = 2 * AUDIO_STREAM_CHUNK_FRAMES;
    int target = (int)ceil(latency_ms * streams->sample_rate / 1000.0);
    
    if (target < min_target)
        target = min_target;
    int max_target = target;
    if (adaptive)
    {
        max_target = (int)ceil(AUDIO_STREAM_ADAPTIVE_MAX_MS * streams->sample_rate / 1000.0);
        if (max_target < target)
            max_target = target;
    }
    int count = input ? AUDIO_MAX_INPUT_STREAMS : AUDIO_MAX_OUTPUT_STREAMS;
    AudioStreamSlot *slots = input ? streams->inputs : streams->outputs;
    for (int id = 0; id < count; id++)
    {
        if (slots[id].active)
            continue;
        AudioStreamSlot *slot = &slots[id];
        memset(slot, 0, sizeof(*slot));

        int capacity = input ? target * 4 : max_target + 2 * AUDIO_STREAM_CHUNK_FRAMES;
        if (!audio_ring_init(&slot->ring, capacity, channels))
            return -1;
        slot->target_frames = target;
        slot->adaptive = adaptive;
        slot->min_target_frames = min_target;
        slot->max_target_frames = max_target;
        slot->low_water = UINT64_MAX;
        slot->active = true;
        return id;
    }
    return -1;
}

void audio_streams_close(AudioStreams *streams, bool input, int id)
{
    AudioStreamSlot *slot = slot_at(streams, input, id);
    if (!slot)
        return;
    slot->active = false;
    audio_ring_free(&slot->ring);
}

bool audio_streams_is_open(const AudioStreams *streams, bool input, int id)
{
    return slot_at((AudioStreams *)streams, input, id) != NULL;
}

bool audio_streams_any_open(const AudioStreams *streams)
{
    if (!streams)
        return false;
    for (int i = 0; i < AUDIO_MAX_OUTPUT_STREAMS; i++)
        if (streams->outputs[i].active)
            return true;
    for (int i = 0; i < AUDIO_MAX_INPUT_STREAMS; i++)
        if (streams->inputs[i].active)
            return true;
    return false;
}

static void adapt_target(AudioStreams *streams, AudioStreamSlot *slot)
{
    uint64_t glitches = audio_ring_load(&slot->glitches);
    uint64_t frames = audio_ring_load(&slot->frames);
    int target = slot->target_frames;
    if (frames < (uint64_t)(streams->sample_rate * AUDIO_STREAM_ADAPTIVE_WARMUP_SECONDS))
    {
        slot->seen_glitches = glitches; 
        slot->seen_missing = audio_ring_load(&slot->missing);
        slot->steady_since = frames;
        audio_ring_store(&slot->low_water, UINT64_MAX);
        return;
    }
    if (glitches > slot->seen_glitches)
    {
        
        uint64_t missing = audio_ring_load(&slot->missing);
        int short_by = (int)(missing - slot->seen_missing);
        slot->seen_glitches = glitches;
        slot->seen_missing = missing;
        short_by = (short_by + AUDIO_STREAM_CHUNK_FRAMES - 1) / AUDIO_STREAM_CHUNK_FRAMES * AUDIO_STREAM_CHUNK_FRAMES;
        target += short_by + AUDIO_STREAM_ADAPTIVE_GROW * AUDIO_STREAM_CHUNK_FRAMES;
        slot->steady_since = frames;
        audio_ring_store(&slot->low_water, UINT64_MAX);
    }
    else if (frames - slot->steady_since > (uint64_t)streams->sample_rate * AUDIO_STREAM_ADAPTIVE_WINDOW_SECONDS)
    {
        uint64_t low = audio_ring_load(&slot->low_water);
        if (low != UINT64_MAX && low > (uint64_t)AUDIO_STREAM_CHUNK_FRAMES)
        {
            int spare = (int)(low - AUDIO_STREAM_CHUNK_FRAMES);
            int shrink = spare / 2 / AUDIO_STREAM_CHUNK_FRAMES * AUDIO_STREAM_CHUNK_FRAMES;
            target -= shrink > AUDIO_STREAM_CHUNK_FRAMES ? shrink : AUDIO_STREAM_CHUNK_FRAMES;
        }
        slot->steady_since = frames;
        audio_ring_store(&slot->low_water, UINT64_MAX);
    }
    if (target < slot->min_target_frames)
        target = slot->min_target_frames;
    if (target > slot->max_target_frames)
        target = slot->max_target_frames;
    slot->target_frames = target;
}

int audio_streams_output_wanted(AudioStreams *streams, int id)
{
    AudioStreamSlot *slot = slot_at(streams, false, id);
    if (!slot)
        return 0;
    if (slot->adaptive)
        adapt_target(streams, slot);
    int wanted = slot->target_frames - audio_ring_queued(&slot->ring);
    return wanted > 0 ? wanted : 0;
}

int audio_streams_output_write(AudioStreams *streams, int id, const float *frames, int count)
{
    AudioStreamSlot *slot = slot_at(streams, false, id);
    if (!slot || !frames || count <= 0)
        return 0;
    int written = audio_ring_write(&slot->ring, frames, count);
    if (written > 0)
        audio_ring_store(&slot->primed, 1);
    return written;
}

int audio_streams_input_available(AudioStreams *streams, int id)
{
    AudioStreamSlot *slot = slot_at(streams, true, id);
    return slot ? audio_ring_queued(&slot->ring) : 0;
}

int audio_streams_input_read(AudioStreams *streams, int id, float *frames, int count)
{
    AudioStreamSlot *slot = slot_at(streams, true, id);
    if (!slot || !frames || count <= 0)
        return 0;
    return audio_ring_read(&slot->ring, frames, count);
}

bool audio_streams_stats(AudioStreams *streams, bool input, int id, AudioStreamStats *out)
{
    AudioStreamSlot *slot = slot_at(streams, input, id);
    if (!slot || !out)
        return false;
    out->sample_rate = streams->sample_rate;
    out->channels = slot->ring.channels;
    out->latency_frames = slot->target_frames;
    out->queued_frames = audio_ring_queued(&slot->ring);
    out->frames = audio_ring_load(&slot->frames);
    out->glitches = audio_ring_load(&slot->glitches);
    return true;
}

static void mix_slot(AudioStreams *streams, AudioStreamSlot *slot, float *out, int frames, int channels)
{
    int in_channels = slot->ring.channels;
    int done = 0, played = 0;
    bool short_read = false;
    if (slot->adaptive && audio_ring_load(&slot->primed))
    {
        uint64_t queued = (uint64_t)audio_ring_queued(&slot->ring);
        if (queued < audio_ring_load(&slot->low_water))
            audio_ring_store(&slot->low_water, queued);
    }
    while (done < frames)
    {
        int want = frames - done;
        if (want > streams->scratch_frames)
            want = streams->scratch_frames;
        int got = audio_ring_read(&slot->ring, streams->mix_scratch, want);
        if (got < want)
            short_read = true;
        played += got;
        float *dst = out + (size_t)done * (size_t)channels;
        const float *src = streams->mix_scratch;
        for (int f = 0; f < got; f++)
        {
            if (in_channels == channels)
            {
                for (int c = 0; c < channels; c++)
                    dst[f * channels + c] += src[f * in_channels + c];
            }
            else if (in_channels == 1)
            {
                dst[f * channels] += src[f];
                dst[f * channels + 1] += src[f];
            }
            else
                dst[f] += 0.5f * (src[f * 2] + src[f * 2 + 1]);
        }
        done += want;
        if (got < want)
            break; 
    }
    if (short_read && audio_ring_load(&slot->primed))
    {
        
        if (!slot->starving)
            audio_ring_store(&slot->glitches, audio_ring_load(&slot->glitches) + 1);
        audio_ring_store(&slot->missing, audio_ring_load(&slot->missing) + (uint64_t)(frames - played));
    }
    slot->starving = short_read;
    audio_ring_store(&slot->frames, audio_ring_load(&slot->frames) + (uint64_t)frames);
}

void audio_streams_mix(AudioStreams *streams, float *out, int frames, int channels)
{
    if (!streams || !streams->mix_scratch || !out || frames <= 0 || channels < 1 || channels > 2)
        return;
    for (int i = 0; i < AUDIO_MAX_OUTPUT_STREAMS; i++)
        if (streams->outputs[i].active)
            mix_slot(streams, &streams->outputs[i], out, frames, channels);
}

void audio_streams_capture(AudioStreams *streams, const float *in, int frames, int channels)
{
    if (!streams || !streams->capture_scratch || !in || frames <= 0 || channels < 1 || channels > 2)
        return;
    for (int i = 0; i < AUDIO_MAX_INPUT_STREAMS; i++)
    {
        AudioStreamSlot *slot = &streams->inputs[i];
        if (!slot->active)
            continue;
        int out_channels = slot->ring.channels;
        uint64_t dropped = 0;
        for (int done = 0; done < frames;)
        {
            int run = frames - done;
            if (run > streams->scratch_frames)
                run = streams->scratch_frames;
            const float *src = in + (size_t)done * (size_t)channels;
            const float *chunk = src;
            if (out_channels != channels)
            {
                for (int f = 0; f < run; f++)
                {
                    if (out_channels == 1)
                        streams->capture_scratch[f] = 0.5f * (src[f * 2] + src[f * 2 + 1]);
                    else
                        streams->capture_scratch[f * 2] = streams->capture_scratch[f * 2 + 1] = src[f];
                }
                chunk = streams->capture_scratch;
            }
            int written = audio_ring_write(&slot->ring, chunk, run);
            dropped += (uint64_t)(run - written);
            done += run;
        }
        if (dropped)
            audio_ring_store(&slot->glitches, audio_ring_load(&slot->glitches) + 1);
        audio_ring_store(&slot->frames, audio_ring_load(&slot->frames) + (uint64_t)frames);
    }
}

bool audio_streams_take_wake(AudioStreams *streams)
{
    if (!streams || audio_ring_load(&streams->wake_pending))
        return false;
    bool low = false;
    for (int i = 0; i < AUDIO_MAX_OUTPUT_STREAMS && !low; i++)
    {
        AudioStreamSlot *slot = &streams->outputs[i];
        if (slot->active && audio_ring_queued(&slot->ring) < slot->target_frames / 2)
            low = true;
    }
    for (int i = 0; i < AUDIO_MAX_INPUT_STREAMS && !low; i++)
    {
        AudioStreamSlot *slot = &streams->inputs[i];
        if (slot->active && audio_ring_queued(&slot->ring) >= AUDIO_STREAM_CHUNK_FRAMES)
            low = true;
    }
    if (low)
        audio_ring_store(&streams->wake_pending, 1);
    return low;
}

void audio_streams_clear_wake(AudioStreams *streams)
{
    if (streams)
        audio_ring_store(&streams->wake_pending, 0);
}