#include "video/video_playback_clock.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_audio_clock_uses_played_frames(void)
{
    VideoPlaybackClock clock;
    char error[128];
    int64_t position;
    video_playback_clock_init(&clock);
    assert(video_playback_clock_start_audio(&clock, 250000, 900000, 48000,
                                            error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 0, 948000, &position,
                                         error, sizeof(error)));
    assert(position == 1250000);
    assert(video_playback_clock_position(&clock, 999999999, 972000,
                                         &position, error, sizeof(error)));
    assert(position == 1750000);
    assert(!video_playback_clock_position(&clock, 0, 899999, &position,
                                          error, sizeof(error)));
}

static void test_pause_resume_and_seek(void)
{
    VideoPlaybackClock clock;
    char error[128];
    int64_t position;
    video_playback_clock_init(&clock);
    assert(video_playback_clock_start_audio(&clock, 1000000, 4000, 40000,
                                            error, sizeof(error)));
    assert(video_playback_clock_pause(&clock, 0, 14000, error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 0, 90000, &position,
                                         error, sizeof(error)));
    assert(position == 1250000);
    assert(video_playback_clock_resume(&clock, 0, 50000, error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 0, 51000, &position,
                                         error, sizeof(error)));
    assert(position == 1275000);
    assert(video_playback_clock_seek(&clock, 8000000, 0, 51000,
                                     error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 0, 55000, &position,
                                         error, sizeof(error)));
    assert(position == 8100000);
    assert(video_playback_clock_pause(&clock, 0, 55000, error, sizeof(error)));
    assert(video_playback_clock_seek(&clock, 3000000, 0, 55000,
                                     error, sizeof(error)));
    assert(video_playback_clock_resume(&clock, 0, 60000, error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 0, 64000, &position,
                                         error, sizeof(error)));
    assert(position == 3100000);
}

static void test_monotonic_fallback_and_failures(void)
{
    VideoPlaybackClock clock;
    char error[128];
    int64_t position;
    video_playback_clock_init(&clock);
    assert(!video_playback_clock_position(&clock, 1, 1, &position,
                                          error, sizeof(error)));
    assert(strcmp(error, "Playback clock is stopped") == 0);
    assert(video_playback_clock_start_monotonic(&clock, -100, 1000,
                                                error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 1500, 0, &position,
                                         error, sizeof(error)));
    assert(position == 400);
    assert(!video_playback_clock_position(&clock, 999, 0, &position,
                                          error, sizeof(error)));
    assert(video_playback_clock_start_monotonic(&clock, INT64_MAX - 1, 0,
                                                error, sizeof(error)));
    assert(!video_playback_clock_position(&clock, 2, 0, &position,
                                          error, sizeof(error)));
    assert(!video_playback_clock_start_audio(&clock, 0, 0, 0,
                                             error, sizeof(error)));
    assert(video_playback_clock_start_audio(&clock, 0, 0, 1,
                                            error, sizeof(error)));
    assert(!video_playback_clock_position(&clock, 0, UINT64_MAX, &position,
                                          error, sizeof(error)));
    assert(video_playback_clock_start_monotonic(&clock, 100, 1000,
                                                error, sizeof(error)));
    assert(video_playback_clock_pause(&clock, 2000, 0, error, sizeof(error)));
    assert(video_playback_clock_resume(&clock, 5000, 0, error, sizeof(error)));
    assert(video_playback_clock_position(&clock, 5100, 0, &position,
                                         error, sizeof(error)));
    assert(position == 1200);
    video_playback_clock_stop(&clock);
    assert(!video_playback_clock_resume(&clock, 0, 0, error, sizeof(error)));
}

int main(void)
{
    test_audio_clock_uses_played_frames();
    test_pause_resume_and_seek();
    test_monotonic_fallback_and_failures();
    return 0;
}