#include "video/video_playback_clock.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>

static void set_error(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
}

static int add_duration(int64_t start, uint64_t duration, int64_t *result)
{
    if (duration > (uint64_t)INT64_MAX ||
        start > INT64_MAX - (int64_t)duration)
        return 0;
    *result = start + (int64_t)duration;
    return 1;
}

static int monotonic_duration(int64_t start, int64_t now, uint64_t *duration)
{
    if (now < start)
        return 0;
    if (start < 0 && now > INT64_MAX + start)
        return 0;
    *duration = (uint64_t)(now - start);
    return 1;
}

static int audio_duration(uint64_t start_frame, uint64_t frames,
                          int sample_rate, uint64_t *duration_us)
{
    uint64_t delta;
    uint64_t whole_seconds;
    uint64_t remaining_frames;

    if (frames < start_frame || sample_rate <= 0)
        return 0;
    delta = frames - start_frame;
    whole_seconds = delta / (uint64_t)sample_rate;
    remaining_frames = delta % (uint64_t)sample_rate;
    if (whole_seconds > (uint64_t)INT64_MAX / 1000000)
        return 0;
    *duration_us = whole_seconds * 1000000 +
                   remaining_frames * 1000000 / (uint64_t)sample_rate;
    return 1;
}

void video_playback_clock_init(VideoPlaybackClock *clock)
{
    if (clock)
    {
        clock->mode = VIDEO_PLAYBACK_CLOCK_STOPPED;
        clock->resume_mode = VIDEO_PLAYBACK_CLOCK_STOPPED;
        clock->media_anchor_us = 0;
        clock->monotonic_anchor_us = 0;
        clock->audio_frame_anchor = 0;
        clock->audio_sample_rate = 0;
    }
}

int video_playback_clock_start_monotonic(VideoPlaybackClock *clock,
                                         int64_t media_time_us,
                                         int64_t monotonic_time_us,
                                         char *error, size_t error_size)
{
    if (!clock)
    {
        set_error(error, error_size, "Invalid playback clock");
        return 0;
    }
    clock->mode = VIDEO_PLAYBACK_CLOCK_MONOTONIC;
    clock->resume_mode = VIDEO_PLAYBACK_CLOCK_MONOTONIC;
    clock->media_anchor_us = media_time_us;
    clock->monotonic_anchor_us = monotonic_time_us;
    clock->audio_frame_anchor = 0;
    clock->audio_sample_rate = 0;
    set_error(error, error_size, "");
    return 1;
}

int video_playback_clock_start_audio(VideoPlaybackClock *clock,
                                     int64_t media_time_us,
                                     uint64_t output_frames,
                                     int sample_rate,
                                     char *error, size_t error_size)
{
    if (!clock || sample_rate <= 0)
    {
        set_error(error, error_size, "Invalid audio playback clock");
        return 0;
    }
    clock->mode = VIDEO_PLAYBACK_CLOCK_AUDIO;
    clock->resume_mode = VIDEO_PLAYBACK_CLOCK_AUDIO;
    clock->media_anchor_us = media_time_us;
    clock->audio_frame_anchor = output_frames;
    clock->audio_sample_rate = sample_rate;
    set_error(error, error_size, "");
    return 1;
}

int video_playback_clock_position(const VideoPlaybackClock *clock,
                                  int64_t monotonic_time_us,
                                  uint64_t output_frames,
                                  int64_t *media_time_us,
                                  char *error, size_t error_size)
{
    uint64_t duration_us;

    if (!clock || !media_time_us)
    {
        set_error(error, error_size, "Invalid playback clock position request");
        return 0;
    }
    if (clock->mode == VIDEO_PLAYBACK_CLOCK_PAUSED)
    {
        *media_time_us = clock->media_anchor_us;
        set_error(error, error_size, "");
        return 1;
    }
    if (clock->mode == VIDEO_PLAYBACK_CLOCK_MONOTONIC)
    {
        if (!monotonic_duration(clock->monotonic_anchor_us,
                                monotonic_time_us, &duration_us))
        {
            set_error(error, error_size, "Monotonic playback clock moved backward or overflowed");
            return 0;
        }
    }
    else if (clock->mode == VIDEO_PLAYBACK_CLOCK_AUDIO)
    {
        if (!audio_duration(clock->audio_frame_anchor, output_frames,
                            clock->audio_sample_rate, &duration_us))
        {
            set_error(error, error_size, "Audio playback frame counter moved backward or overflowed");
            return 0;
        }
    }
    else
    {
        set_error(error, error_size, "Playback clock is stopped");
        return 0;
    }
    if (!add_duration(clock->media_anchor_us, duration_us, media_time_us))
    {
        set_error(error, error_size, "Playback time overflow");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int video_playback_clock_pause(VideoPlaybackClock *clock,
                               int64_t monotonic_time_us,
                               uint64_t output_frames,
                               char *error, size_t error_size)
{
    int64_t media_time_us;
    if (!clock || (clock->mode != VIDEO_PLAYBACK_CLOCK_MONOTONIC &&
                   clock->mode != VIDEO_PLAYBACK_CLOCK_AUDIO))
    {
        set_error(error, error_size, "Playback clock is not running");
        return 0;
    }
    if (!video_playback_clock_position(clock, monotonic_time_us, output_frames,
                                       &media_time_us, error, error_size))
        return 0;
    clock->resume_mode = clock->mode;
    clock->mode = VIDEO_PLAYBACK_CLOCK_PAUSED;
    clock->media_anchor_us = media_time_us;
    set_error(error, error_size, "");
    return 1;
}

int video_playback_clock_resume(VideoPlaybackClock *clock,
                                int64_t monotonic_time_us,
                                uint64_t output_frames,
                                char *error, size_t error_size)
{
    if (!clock || clock->mode != VIDEO_PLAYBACK_CLOCK_PAUSED ||
        (clock->resume_mode != VIDEO_PLAYBACK_CLOCK_MONOTONIC &&
         clock->resume_mode != VIDEO_PLAYBACK_CLOCK_AUDIO))
    {
        set_error(error, error_size, "Playback clock is not paused");
        return 0;
    }
    if (clock->resume_mode == VIDEO_PLAYBACK_CLOCK_MONOTONIC)
        clock->monotonic_anchor_us = monotonic_time_us;
    else
        clock->audio_frame_anchor = output_frames;
    clock->mode = clock->resume_mode;
    set_error(error, error_size, "");
    return 1;
}

int video_playback_clock_seek(VideoPlaybackClock *clock,
                              int64_t media_time_us,
                              int64_t monotonic_time_us,
                              uint64_t output_frames,
                              char *error, size_t error_size)
{
    if (!clock || clock->mode == VIDEO_PLAYBACK_CLOCK_STOPPED)
    {
        set_error(error, error_size, "Playback clock is stopped");
        return 0;
    }
    clock->media_anchor_us = media_time_us;
    if (clock->mode == VIDEO_PLAYBACK_CLOCK_MONOTONIC)
        clock->monotonic_anchor_us = monotonic_time_us;
    else if (clock->mode == VIDEO_PLAYBACK_CLOCK_AUDIO)
        clock->audio_frame_anchor = output_frames;
    set_error(error, error_size, "");
    return 1;
}

void video_playback_clock_stop(VideoPlaybackClock *clock)
{
    video_playback_clock_init(clock);
}