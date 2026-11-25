#ifndef BUDO_VIDEO_PLAYER_H
#define BUDO_VIDEO_PLAYER_H

#include "video/video_composition.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct FileContext FileContext;
typedef struct Window Window;
typedef struct VideoPlayer VideoPlayer;

typedef enum VideoPlayerEventType
{
    VIDEO_PLAYER_FRAME,
    VIDEO_PLAYER_END,
    VIDEO_PLAYER_ERROR
} VideoPlayerEventType;

typedef struct VideoPlayerEvent
{
    VideoPlayerEventType type;
    int texture_id;
    int64_t pts_us;
    int64_t duration_us;
    char code[32];
    char message[256];
} VideoPlayerEvent;

typedef enum VideoPlayerCloseResult
{
    VIDEO_PLAYER_CLOSE_PENDING,
    VIDEO_PLAYER_CLOSE_DONE,
    VIDEO_PLAYER_CLOSE_ERROR
} VideoPlayerCloseResult;

VideoPlayer *video_player_open_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, char *error, size_t error_size);

VideoPlayer *video_player_open_files_at(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, int64_t start_us, bool paused,
    char *error, size_t error_size);

bool video_player_poll(VideoPlayer *player, Window *window,
                       int64_t monotonic_time_us, VideoPlayerEvent *event);
bool video_player_pending(VideoPlayer *player);
int video_player_texture_id(const VideoPlayer *player);

void video_player_context_lost(VideoPlayer *player);

bool video_player_pause(VideoPlayer *player, int64_t monotonic_time_us,
                        char *error, size_t error_size);
bool video_player_resume(VideoPlayer *player, int64_t monotonic_time_us,
                         char *error, size_t error_size);

void video_player_request_close(VideoPlayer *player);
VideoPlayerCloseResult video_player_try_close(VideoPlayer *player,
                                               Window *window);

bool video_player_close(VideoPlayer *player, Window *window);

void video_player_close_without_graphics(VideoPlayer *player);

#endif