#ifndef BUDO_VIDEO_COMPOSITOR_H
#define BUDO_VIDEO_COMPOSITOR_H

#include "video/video_demux.h"

#include <stddef.h>
#include <stdint.h>

typedef struct VideoCompositionConfig
{
    int output_width;
    int output_height;
    int inset_x;
    int inset_y;
    int inset_width;
    int inset_height;
} VideoCompositionConfig;

int video_compose_rgba(const VideoFrame *background, const VideoFrame *inset,
                       const VideoCompositionConfig *config, uint8_t *output,
                       size_t output_size, char *error, size_t error_size);

#endif