#include "video/video_compositor.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define VIDEO_COMPOSITOR_MAX_PIXELS (16u * 1024u * 1024u)

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

static int valid_frame(const VideoFrame *frame)
{
    if (!frame || !frame->rgba || frame->width <= 0 || frame->height <= 0 ||
        frame->width > INT_MAX / 4 || frame->stride < frame->width * 4 ||
        (size_t)frame->width * (size_t)frame->height > VIDEO_COMPOSITOR_MAX_PIXELS)
        return 0;
    size_t minimum = (size_t)frame->stride * (size_t)(frame->height - 1) +
                     (size_t)frame->width * 4u;
    return frame->size >= minimum;
}

static void scale_frame(const VideoFrame *source, uint8_t *destination,
                        int destination_width, int destination_height,
                        int destination_stride)
{
    for (int y = 0; y < destination_height; ++y)
    {
        int source_y = (int)(((int64_t)y * source->height) / destination_height);
        const uint8_t *source_row =
            source->rgba + (size_t)source_y * (size_t)source->stride;
        uint8_t *destination_row =
            destination + (size_t)y * (size_t)destination_stride;
        for (int x = 0; x < destination_width; ++x)
        {
            int source_x = (int)(((int64_t)x * source->width) / destination_width);
            memcpy(destination_row + (size_t)x * 4u,
                   source_row + (size_t)source_x * 4u, 4u);
        }
    }
}

int video_compose_rgba(const VideoFrame *background, const VideoFrame *inset,
                       const VideoCompositionConfig *config, uint8_t *output,
                       size_t output_size, char *error, size_t error_size)
{
    if (!valid_frame(background) || !valid_frame(inset) || !config || !output)
    {
        set_error(error, error_size, "Invalid composition frame or argument");
        return 0;
    }
    if (config->output_width <= 0 || config->output_height <= 0 ||
        config->output_width > INT_MAX / 4 ||
        (size_t)config->output_width * (size_t)config->output_height >
            VIDEO_COMPOSITOR_MAX_PIXELS ||
        config->inset_x < 0 || config->inset_y < 0 ||
        config->inset_width <= 0 || config->inset_height <= 0 ||
        config->inset_x > config->output_width - config->inset_width ||
        config->inset_y > config->output_height - config->inset_height)
    {
        set_error(error, error_size, "Invalid composition geometry");
        return 0;
    }

    size_t required = (size_t)config->output_width * 4u *
                      (size_t)config->output_height;
    if (output_size < required)
    {
        set_error(error, error_size, "Composition output buffer is too small");
        return 0;
    }

    scale_frame(background, output, config->output_width, config->output_height,
                config->output_width * 4);
    for (int y = 0; y < config->inset_height; ++y)
    {
        uint8_t *destination =
            output + ((size_t)(config->inset_y + y) * config->output_width +
                      (size_t)config->inset_x) * 4u;
        int source_y = (int)(((int64_t)y * inset->height) /
                             config->inset_height);
        const uint8_t *source_row =
            inset->rgba + (size_t)source_y * (size_t)inset->stride;
        for (int x = 0; x < config->inset_width; ++x)
        {
            int source_x = (int)(((int64_t)x * inset->width) /
                                 config->inset_width);
            memcpy(destination + (size_t)x * 4u,
                   source_row + (size_t)source_x * 4u, 4u);
        }
    }
    set_error(error, error_size, "");
    return 1;
}