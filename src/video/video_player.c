#include "video/video_player.h"

#include "video/video_playback_clock.h"
#include "video/video_preview.h"
#include "video/video_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_PLAYER_FRAME_INTERVAL_US 33334

struct VideoPlayer
{
    VideoPreview *preview;
    VideoTexture texture;
    VideoPlaybackClock clock;
    bool clock_started;
    bool terminal;
    bool closing;
    bool end_pending;
    bool pause_on_start;
    bool allow_prestart_pause;
    int64_t start_us;
    int64_t last_frame_pts_us;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

static void set_event(VideoPlayerEvent *event, VideoPlayerEventType type,
                      const char *code, const char *message, int texture_id,
                      int64_t pts_us)
{
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->texture_id = texture_id;
    event->pts_us = pts_us;
    if (code)
        snprintf(event->code, sizeof(event->code), "%s", code);
    if (message)
        snprintf(event->message, sizeof(event->message), "%s", message);
}

VideoPlayer *video_player_open_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, char *error, size_t error_size)
{
    VideoPlayer *player = video_player_open_files_at(
        files, background_path, inset_path, config, 0, false, error, error_size);
    if (player)
        player->allow_prestart_pause = false;
    return player;
}

VideoPlayer *video_player_open_files_at(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, int64_t start_us, bool paused,
    char *error, size_t error_size)
{
    VideoPlayer *player = calloc(1, sizeof(*player));
    if (!player)
    {
        set_error(error, error_size, "Cannot allocate video player");
        return NULL;
    }

    player->preview = video_preview_open_files_at(
        files, background_path, inset_path, config, start_us, error, error_size);
    if (!player->preview)
    {
        free(player);
        return NULL;
    }
    video_playback_clock_init(&player->clock);
    player->start_us = start_us;
    player->pause_on_start = paused;
    player->allow_prestart_pause = true;
    set_error(error, error_size, "");
    return player;
}

bool video_player_poll(VideoPlayer *player, Window *window,
                       int64_t monotonic_time_us, VideoPlayerEvent *event)
{
    if (!event)
        return false;
    memset(event, 0, sizeof(*event));
    if (!player || player->terminal || player->closing)
        return false;
    if (!window)
    {
        player->terminal = true;
        video_playback_clock_stop(&player->clock);
        set_event(event, VIDEO_PLAYER_ERROR, "video.graphics_unavailable",
                  "Video playback requires the owning graphics context",
                  player->texture.texture_id, 0);
        return true;
    }

    int64_t media_time_us = player->start_us;
    char error[256] = {0};
    if (player->clock_started &&
        !video_playback_clock_position(&player->clock, monotonic_time_us, 0,
                                       &media_time_us, error, sizeof(error)))
    {
        player->terminal = true;
        video_playback_clock_stop(&player->clock);
        set_event(event, VIDEO_PLAYER_ERROR, "video.clock_failed", error,
                  player->texture.texture_id, 0);
        return true;
    }

    if (player->end_pending)
    {
        if (media_time_us - player->last_frame_pts_us < VIDEO_PLAYER_FRAME_INTERVAL_US)
            return false;
        player->terminal = true;
        video_playback_clock_stop(&player->clock);
        set_event(event, VIDEO_PLAYER_END, NULL, NULL,
                  player->texture.texture_id, 0);
        return true;
    }

    VideoPreviewEvent preview_event;
    if (!video_preview_poll_latest(player->preview, media_time_us,
                                  &preview_event))
        return false;

    if (preview_event.type == VIDEO_PREVIEW_FRAME)
    {
        if (!video_texture_upload(window, &player->texture,
                                  &preview_event.frame, error, sizeof(error)))
        {
            video_preview_event_free(&preview_event);
            player->terminal = true;
            video_playback_clock_stop(&player->clock);
            set_event(event, VIDEO_PLAYER_ERROR, "video.texture_upload_failed",
                      error, player->texture.texture_id, 0);
            return true;
        }

        if (!player->clock_started)
        {
            if (!video_playback_clock_start_monotonic(
                    &player->clock, preview_event.pts_us, monotonic_time_us,
                    error, sizeof(error)))
            {
                video_preview_event_free(&preview_event);
                player->terminal = true;
                set_event(event, VIDEO_PLAYER_ERROR, "video.clock_failed",
                          error, player->texture.texture_id, 0);
                return true;
            }
            player->clock_started = true;
            if (player->pause_on_start &&
                !video_playback_clock_pause(&player->clock, monotonic_time_us,
                                            0, error, sizeof(error)))
            {
                video_preview_event_free(&preview_event);
                player->terminal = true;
                set_event(event, VIDEO_PLAYER_ERROR, "video.clock_failed",
                          error, player->texture.texture_id, 0);
                return true;
            }
        }

        set_event(event, VIDEO_PLAYER_FRAME, NULL, NULL,
                  player->texture.texture_id, preview_event.pts_us);
        event->duration_us = preview_event.duration_us;
        player->last_frame_pts_us = preview_event.pts_us;
        video_preview_event_free(&preview_event);
        return true;
    }

    if (preview_event.type == VIDEO_PREVIEW_END && player->clock_started &&
        media_time_us - player->last_frame_pts_us < VIDEO_PLAYER_FRAME_INTERVAL_US)
    {
        player->end_pending = true;
        video_preview_event_free(&preview_event);
        return false;
    }
    player->terminal = true;
    video_playback_clock_stop(&player->clock);
    if (preview_event.type == VIDEO_PREVIEW_END)
        set_event(event, VIDEO_PLAYER_END, NULL, NULL,
                  player->texture.texture_id, 0);
    else
        set_event(event, VIDEO_PLAYER_ERROR, preview_event.code,
                  preview_event.message, player->texture.texture_id, 0);
    video_preview_event_free(&preview_event);
    return true;
}

bool video_player_pending(VideoPlayer *player)
{
    return player && (player->closing ||
                      (!player->terminal && (player->end_pending ||
                                             video_preview_pending(player->preview))));
}

int video_player_texture_id(const VideoPlayer *player)
{
    return player ? player->texture.texture_id : 0;
}

void video_player_context_lost(VideoPlayer *player)
{
    if (!player)
        return;
    memset(&player->texture, 0, sizeof(player->texture));
    player->terminal = true;
    video_playback_clock_stop(&player->clock);
    video_preview_request_stop(player->preview);
}

bool video_player_pause(VideoPlayer *player, int64_t monotonic_time_us,
                        char *error, size_t error_size)
{
    if (player && !player->terminal && !player->closing &&
        !player->clock_started && player->allow_prestart_pause &&
        !player->pause_on_start)
    {
        player->pause_on_start = true;
        set_error(error, error_size, "");
        return true;
    }
    if (!player || player->terminal || player->closing || !player->clock_started)
    {
        set_error(error, error_size, "Video preview has not started");
        return false;
    }
    return video_playback_clock_pause(&player->clock, monotonic_time_us, 0,
                                      error, error_size) != 0;
}

bool video_player_resume(VideoPlayer *player, int64_t monotonic_time_us,
                         char *error, size_t error_size)
{
    if (player && !player->terminal && !player->closing &&
        !player->clock_started && player->allow_prestart_pause &&
        player->pause_on_start)
    {
        player->pause_on_start = false;
        set_error(error, error_size, "");
        return true;
    }
    if (!player || player->terminal || player->closing || !player->clock_started)
    {
        set_error(error, error_size, "Video preview has not started");
        return false;
    }
    return video_playback_clock_resume(&player->clock, monotonic_time_us, 0,
                                       error, error_size) != 0;
}

void video_player_request_close(VideoPlayer *player)
{
    if (!player || player->closing)
        return;
    player->closing = true;
    video_playback_clock_stop(&player->clock);
    video_preview_request_stop(player->preview);
}

VideoPlayerCloseResult video_player_try_close(VideoPlayer *player,
                                               Window *window)
{
    if (!player)
        return VIDEO_PLAYER_CLOSE_DONE;
    video_player_request_close(player);
    if (!video_preview_stopped(player->preview))
        return VIDEO_PLAYER_CLOSE_PENDING;
    return video_player_close(player, window) ? VIDEO_PLAYER_CLOSE_DONE
                                              : VIDEO_PLAYER_CLOSE_ERROR;
}

bool video_player_close(VideoPlayer *player, Window *window)
{
    if (!player)
        return true;
    if (player->preview)
    {
        video_preview_close(player->preview);
        player->preview = NULL;
    }
    if (player->texture.texture_id > 0 && !window)
        return false;
    if (!video_texture_close(window, &player->texture))
        return false;
    free(player);
    return true;
}

void video_player_close_without_graphics(VideoPlayer *player)
{
    if (!player)
        return;
    if (player->preview)
    {
        video_preview_close(player->preview);
        player->preview = NULL;
    }
    player->texture.texture_id = 0;
    player->texture.width = 0;
    player->texture.height = 0;
    free(player);
}