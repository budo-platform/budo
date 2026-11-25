#ifndef BUDO_VIDEO_PREVIEW_H
#define BUDO_VIDEO_PREVIEW_H

#include "video/video_composition.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct VideoPreview VideoPreview;

typedef enum VideoPreviewEventType
{
    VIDEO_PREVIEW_FRAME,
    VIDEO_PREVIEW_END,
    VIDEO_PREVIEW_ERROR
} VideoPreviewEventType;

typedef struct VideoPreviewEvent
{
    VideoPreviewEventType type;
    VideoFrame frame;
    uint8_t *pixels;
    int64_t pts_us;
    int64_t duration_us;
    char code[32];
    char message[256];
} VideoPreviewEvent;

VideoPreview *video_preview_open_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, char *error, size_t error_size);

VideoPreview *video_preview_open_files_at(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, int64_t start_us,
    char *error, size_t error_size);

bool video_preview_poll_latest(VideoPreview *preview, int64_t media_time_us,
                               VideoPreviewEvent *event);
void video_preview_event_free(VideoPreviewEvent *event);
bool video_preview_pending(VideoPreview *preview);

void video_preview_request_stop(VideoPreview *preview);
bool video_preview_stopped(VideoPreview *preview);

void video_preview_close(VideoPreview *preview);

#endif