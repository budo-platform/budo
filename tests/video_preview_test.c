#include "file/file_wrapper.h"
#include "video/video_preview.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void wait_a_little(void)
{
    Sleep(1);
}
#else
#include <time.h>
static void wait_a_little(void)
{
    const struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
#endif

static bool poll_until(VideoPreview *preview, int64_t media_time_us,
                       VideoPreviewEvent *event)
{
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        if (video_preview_poll_latest(preview, media_time_us, event))
            return true;
        if (!video_preview_pending(preview))
            return false;
        wait_a_little();
    }
    return false;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FileContext *files = file_create(argv[1]);
    assert(files);
    VideoCompositionConfig config = {640, 360, 464, 254, 160, 90};
    char error[256];
    VideoCompositionConfig invalid_config = {INT_MAX, 360, 0, 0, 1, 1};
    assert(!video_preview_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &invalid_config, error, sizeof(error)));
    assert(error[0] != '\0');

    VideoPreview *preview = video_preview_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    if (!preview)
        fprintf(stderr, "%s\n", error);
    assert(preview && error[0] == '\0');

    int frame_count = 0;
    for (int i = 0; i < 60; ++i)
    {
        VideoPreviewEvent event;
        int64_t target_us = ((int64_t)i * 1000000 + 15) / 30;
        assert(poll_until(preview, target_us, &event));
        assert(event.type == VIDEO_PREVIEW_FRAME);
        assert(event.pts_us == target_us);
        assert(event.frame.width == 640 && event.frame.height == 360);
        assert(event.frame.stride == 640 * 4);
        assert(event.frame.rgba == event.pixels);
        const uint8_t *background =
            event.pixels + ((size_t)20 * 640 + 20) * 4;
        const uint8_t *inset =
            event.pixels + ((size_t)300 * 640 + 500) * 4;
        assert(background[0] > 220 && background[1] < 35 &&
               background[2] < 35);
        assert(inset[0] < 35 && inset[1] < 35 && inset[2] > 220);
        video_preview_event_free(&event);
        frame_count++;
    }
    assert(frame_count == 60);
    VideoPreviewEvent terminal;
    assert(poll_until(preview, INT64_MAX, &terminal));
    assert(terminal.type == VIDEO_PREVIEW_END);
    video_preview_event_free(&terminal);
    assert(!video_preview_pending(preview));
    video_preview_close(preview);

    preview = video_preview_open_files_at(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, 1500000, error, sizeof(error));
    assert(preview);
    VideoPreviewEvent sought;
    assert(poll_until(preview, 1500000, &sought));
    assert(sought.type == VIDEO_PREVIEW_FRAME);
    assert(sought.pts_us == 1500000);
    video_preview_event_free(&sought);
    video_preview_request_stop(preview);
    for (int attempt = 0; attempt < 10000 && !video_preview_stopped(preview); ++attempt)
        wait_a_little();
    assert(video_preview_stopped(preview));
    video_preview_close(preview);

    preview = video_preview_open_files(
        files, "assets/missing.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(preview);
    assert(poll_until(preview, INT64_MAX, &terminal));
    assert(terminal.type == VIDEO_PREVIEW_ERROR);
    assert(strcmp(terminal.code, "video.open_failed") == 0);
    assert(terminal.message[0] != '\0');
    video_preview_event_free(&terminal);
    video_preview_close(preview);

    preview = video_preview_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(preview);
    video_preview_request_stop(preview);
    video_preview_request_stop(preview);
    for (int attempt = 0; attempt < 10000 && !video_preview_stopped(preview); ++attempt)
        wait_a_little();
    assert(video_preview_stopped(preview));
    video_preview_close(preview);
    file_destroy(files);
    return 0;
}