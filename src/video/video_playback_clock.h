#ifndef BUDO_VIDEO_PLAYBACK_CLOCK_H
#define BUDO_VIDEO_PLAYBACK_CLOCK_H

#include <stddef.h>
#include <stdint.h>

typedef enum VideoPlaybackClockMode
{
    VIDEO_PLAYBACK_CLOCK_STOPPED,
    VIDEO_PLAYBACK_CLOCK_MONOTONIC,
    VIDEO_PLAYBACK_CLOCK_AUDIO,
    VIDEO_PLAYBACK_CLOCK_PAUSED
} VideoPlaybackClockMode;

typedef struct VideoPlaybackClock
{
    VideoPlaybackClockMode mode;
    VideoPlaybackClockMode resume_mode;
    int64_t media_anchor_us;
    int64_t monotonic_anchor_us;
    uint64_t audio_frame_anchor;
    int audio_sample_rate;
} VideoPlaybackClock;

void video_playback_clock_init(VideoPlaybackClock *clock);
int video_playback_clock_start_monotonic(VideoPlaybackClock *clock,
                                         int64_t media_time_us,
                                         int64_t monotonic_time_us,
                                         char *error, size_t error_size);
int video_playback_clock_start_audio(VideoPlaybackClock *clock,
                                     int64_t media_time_us,
                                     uint64_t output_frames,
                                     int sample_rate,
                                     char *error, size_t error_size);
int video_playback_clock_position(const VideoPlaybackClock *clock,
                                  int64_t monotonic_time_us,
                                  uint64_t output_frames,
                                  int64_t *media_time_us,
                                  char *error, size_t error_size);
int video_playback_clock_pause(VideoPlaybackClock *clock,
                               int64_t monotonic_time_us,
                               uint64_t output_frames,
                               char *error, size_t error_size);
int video_playback_clock_resume(VideoPlaybackClock *clock,
                                int64_t monotonic_time_us,
                                uint64_t output_frames,
                                char *error, size_t error_size);
int video_playback_clock_seek(VideoPlaybackClock *clock,
                              int64_t media_time_us,
                              int64_t monotonic_time_us,
                              uint64_t output_frames,
                              char *error, size_t error_size);
void video_playback_clock_stop(VideoPlaybackClock *clock);

#endif