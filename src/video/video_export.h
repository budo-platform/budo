#ifndef BUDO_VIDEO_EXPORT_H
#define BUDO_VIDEO_EXPORT_H

#include "video/video_composition.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct VideoExport VideoExport;

typedef enum VideoExportStatus
{
    VIDEO_EXPORT_RUNNING,
    VIDEO_EXPORT_SUCCEEDED,
    VIDEO_EXPORT_FAILED,
    VIDEO_EXPORT_CANCELLED
} VideoExportStatus;

typedef struct VideoExportEvent
{
    VideoExportStatus status;
    int64_t completed_frames;
    char code[32];
    char message[256];
} VideoExportEvent;

VideoExport *video_export_start_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const char *output_path, const VideoCompositionConfig *config,
    int video_bit_rate, char *error, size_t error_size);

bool video_export_poll(VideoExport *export, VideoExportEvent *event);

bool video_export_cancel(VideoExport *export);

void video_export_close(VideoExport *export);

#endif