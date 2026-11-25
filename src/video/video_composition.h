#ifndef BUDO_VIDEO_COMPOSITION_H
#define BUDO_VIDEO_COMPOSITION_H

#include "video/video_compositor.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct VideoComposition VideoComposition;

typedef struct VideoCompositionExportControl
{
    bool (*is_cancelled)(void *context);
    
    bool (*begin_finalization)(void *context);
    void (*progress)(void *context, int64_t completed_frames);
    void *context;
} VideoCompositionExportControl;

VideoComposition *video_composition_open_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, char *error, size_t error_size);

int64_t video_composition_frame_count(const VideoComposition *composition);

int video_composition_seek_frame(VideoComposition *composition,
                                 int64_t ordinal,
                                 char *error, size_t error_size);

int video_composition_next(VideoComposition *composition, uint8_t *output,
                           size_t output_size, int64_t *pts_us,
                           char *error, size_t error_size);

int video_composition_export_file(
    FileContext *files, const char *background_path, const char *inset_path,
    const char *output_path, const VideoCompositionConfig *config,
    int video_bit_rate, char *error, size_t error_size);
int video_composition_export_file_controlled(
    FileContext *files, const char *background_path, const char *inset_path,
    const char *output_path, const VideoCompositionConfig *config,
    int video_bit_rate, const VideoCompositionExportControl *control,
    char *error, size_t error_size);

void video_composition_close(VideoComposition *composition);

#endif