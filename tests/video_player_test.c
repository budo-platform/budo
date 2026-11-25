#include "core/window.h"
#include "file/file_wrapper.h"
#include "video/video_player.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
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

static int created;
static int destroyed;
static bool fail_update;

int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    (void)window;
    assert(width == 640 && height == 360);
    assert(format == WINDOW_GL_TEX_RGBA8 && pixels);
    created++;
    return 40;
}

bool window_gl_update_texture_2d(Window *window, int texture_id, int x, int y,
                                 int width, int height, const void *pixels)
{
    (void)window;
    assert(texture_id == 40 && x == 0 && y == 0);
    assert(width == 640 && height == 360 && pixels);
    return !fail_update;
}

bool window_gl_destroy_texture(Window *window, int texture_id)
{
    (void)window;
    assert(texture_id == 40);
    destroyed++;
    return true;
}

static bool poll_until(VideoPlayer *player, int64_t monotonic_time_us,
                       VideoPlayerEvent *event)
{
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        if (video_player_poll(player, (Window *)1, monotonic_time_us, event))
            return true;
        if (!video_player_pending(player))
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

    VideoPlayer *player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player && error[0] == '\0');

    VideoPlayerEvent event;
    const int64_t start_us = 5000000;
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_FRAME);
    assert(event.pts_us == 0);
    assert(event.texture_id == 40);
    assert(video_player_texture_id(player) == 40);
    assert(created == 1 && destroyed == 0);

    assert(poll_until(player, start_us + 10000000, &event));
    assert(event.type == VIDEO_PLAYER_FRAME || event.type == VIDEO_PLAYER_END);
    while (event.type == VIDEO_PLAYER_FRAME)
    {
        assert(event.pts_us >= 0);
        assert(event.texture_id == 40);
        if (!poll_until(player, start_us + 10000000, &event))
            break;
    }
    assert(event.type == VIDEO_PLAYER_END);
    assert(!video_player_pending(player));
    assert(video_player_close(player, (Window *)1));
    assert(destroyed == 1);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_FRAME && event.pts_us == 0);
    assert(video_player_pause(player, start_us, error, sizeof(error)));
    assert(error[0] == '\0');
    assert(!video_player_poll(player, (Window *)1, start_us + 10000000,
                              &event));
    assert(video_player_resume(player, start_us + 10000000, error,
                               sizeof(error)));
    assert(error[0] == '\0');
    assert(poll_until(player, start_us + 10100000, &event));
    assert(event.type == VIDEO_PLAYER_FRAME && event.pts_us > 0);
    assert(video_player_close(player, (Window *)1));
    assert(destroyed == 2);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    for (int i = 0; i < 60; ++i)
    {
        int64_t pts_us = ((int64_t)i * 1000000 + 15) / 30;
        assert(poll_until(player, start_us + pts_us, &event));
        assert(event.type == VIDEO_PLAYER_FRAME && event.pts_us == pts_us);
    }
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        assert(!video_player_poll(player, (Window *)1, start_us + 1999999, &event));
        assert(video_player_pending(player));
        wait_a_little();
    }
    assert(poll_until(player, start_us + 2000001, &event));
    assert(event.type == VIDEO_PLAYER_END);
    assert(video_player_close(player, (Window *)1));
    assert(destroyed == 3);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_FRAME);
    assert(video_player_poll(player, (Window *)1, start_us - 1, &event));
    assert(event.type == VIDEO_PLAYER_ERROR);
    assert(strcmp(event.code, "video.clock_failed") == 0);
    assert(video_player_close(player, (Window *)1));
    assert(destroyed == 4);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_FRAME);
    fail_update = true;
    assert(poll_until(player, start_us + 10000000, &event));
    fail_update = false;
    assert(event.type == VIDEO_PLAYER_ERROR);
    assert(strcmp(event.code, "video.texture_upload_failed") == 0);
    assert(video_player_close(player, (Window *)1));
    assert(destroyed == 5);

    player = video_player_open_files(
        files, "assets/missing.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_ERROR);
    assert(strcmp(event.code, "video.open_failed") == 0);
    assert(event.message[0] != '\0');
    assert(video_player_close(player, (Window *)1));
    assert(destroyed == 5);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_FRAME);
    video_player_request_close(player);
    video_player_request_close(player);
    assert(video_player_pending(player));
    assert(!video_player_poll(player, (Window *)1, start_us + 10000000, &event));
    VideoPlayerCloseResult close_result = VIDEO_PLAYER_CLOSE_PENDING;
    for (int attempt = 0; attempt < 10000 && close_result == VIDEO_PLAYER_CLOSE_PENDING; ++attempt)
    {
        close_result = video_player_try_close(player, NULL);
        if (close_result == VIDEO_PLAYER_CLOSE_PENDING)
            wait_a_little();
    }
    assert(close_result == VIDEO_PLAYER_CLOSE_ERROR);
    assert(video_player_try_close(player, (Window *)1) == VIDEO_PLAYER_CLOSE_DONE);
    assert(destroyed == 6);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(poll_until(player, start_us, &event));
    assert(event.type == VIDEO_PLAYER_FRAME);
    video_player_request_close(player);
    video_player_close_without_graphics(player);
    assert(destroyed == 6);

    player = video_player_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(player);
    assert(video_player_poll(player, NULL, start_us, &event));
    assert(event.type == VIDEO_PLAYER_ERROR);
    assert(strcmp(event.code, "video.graphics_unavailable") == 0);
    assert(video_player_close(player, (Window *)1));

    file_destroy(files);
    return 0;
}