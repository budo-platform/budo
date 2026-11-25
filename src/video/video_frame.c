#include "video/video_frame.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_OWNED_FRAME_MAX_PIXELS (16u * 1024u * 1024u)

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

void video_owned_frame_init(VideoOwnedFrame *frame)
{
    if (frame)
        memset(frame, 0, sizeof(*frame));
}

void video_owned_frame_release(VideoOwnedFrame *frame)
{
    if (!frame)
        return;
    free(frame->pixels);
    video_owned_frame_init(frame);
}

int video_owned_frame_copy(VideoOwnedFrame *destination,
                           const VideoFrame *source,
                           char *error, size_t error_size)
{
    if (!destination || !source || !source->rgba ||
        source->width <= 0 || source->height <= 0 ||
        source->width > INT_MAX / 4 ||
        source->stride < source->width * 4 ||
        (size_t)source->width >
            VIDEO_OWNED_FRAME_MAX_PIXELS / (size_t)source->height)
    {
        set_error(error, error_size, "Invalid or oversized video frame");
        return 0;
    }

    size_t row_size = (size_t)source->width * 4u;
    if (source->height > 1 &&
        (size_t)source->stride >
            (SIZE_MAX - row_size) / (size_t)(source->height - 1))
    {
        set_error(error, error_size, "Video frame buffer size overflows");
        return 0;
    }

    size_t minimum_size =
        (size_t)source->stride * (size_t)(source->height - 1) +
        row_size;
    if (source->size < minimum_size)
    {
        set_error(error, error_size, "Video frame buffer is too small");
        return 0;
    }

    size_t stride = row_size;
    size_t size = stride * (size_t)source->height;
    uint8_t *pixels = malloc(size);
    if (!pixels)
    {
        set_error(error, error_size, "Cannot allocate owned video frame");
        return 0;
    }

    for (int y = 0; y < source->height; ++y)
        memcpy(pixels + (size_t)y * stride,
               source->rgba + (size_t)y * (size_t)source->stride, stride);

    VideoOwnedFrame replacement = {
        .frame = *source,
        .pixels = pixels,
    };
    replacement.frame.rgba = pixels;
    replacement.frame.stride = (int)stride;
    replacement.frame.size = size;
    free(destination->pixels);
    *destination = replacement;
    set_error(error, error_size, "");
    return 1;
}