#include "video/video_texture.h"
#include "core/window.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

int video_texture_upload(Window *window, VideoTexture *texture,
                         const VideoFrame *frame, char *error, size_t error_size)
{
    if (!window || !texture || !frame || !frame->rgba ||
        frame->width <= 0 || frame->height <= 0 ||
        frame->width > INT_MAX / 4 ||
        (size_t)frame->width > SIZE_MAX / 4)
    {
        set_error(error, error_size, "Invalid video frame texture upload");
        return 0;
    }

    size_t row_size = (size_t)frame->width * 4;
    if ((size_t)frame->height > SIZE_MAX / row_size ||
        frame->stride != (int)row_size ||
        frame->size < row_size * (size_t)frame->height)
    {
        set_error(error, error_size, "Video frame is not tightly packed RGBA8");
        return 0;
    }

    if (texture->texture_id > 0 &&
        texture->width == frame->width && texture->height == frame->height)
    {
        if (!window_gl_update_texture_2d(window, texture->texture_id, 0, 0,
                                          frame->width, frame->height, frame->rgba))
        {
            set_error(error, error_size, "Cannot update video frame texture");
            return 0;
        }
        set_error(error, error_size, "");
        return 1;
    }

    int replacement = window_gl_create_texture_2d(window, frame->width, frame->height,
                                                   WINDOW_GL_TEX_RGBA8, frame->rgba);
    if (replacement <= 0)
    {
        set_error(error, error_size, "Cannot create video frame texture");
        return 0;
    }

    int previous = texture->texture_id;
    if (previous > 0 && !window_gl_destroy_texture(window, previous))
    {
        window_gl_destroy_texture(window, replacement);
        set_error(error, error_size, "Cannot replace video frame texture");
        return 0;
    }
    texture->texture_id = replacement;
    texture->width = frame->width;
    texture->height = frame->height;
    set_error(error, error_size, "");
    return 1;
}

int video_texture_close(Window *window, VideoTexture *texture)
{
    if (!texture)
        return 0;
    if (texture->texture_id > 0 && !window)
        return 0;
    if (texture->texture_id > 0 &&
        !window_gl_destroy_texture(window, texture->texture_id))
        return 0;
    texture->texture_id = 0;
    texture->width = 0;
    texture->height = 0;
    return 1;
}