#ifndef BUDO_VIDEO_TEXTURE_H
#define BUDO_VIDEO_TEXTURE_H

#include "video/video_demux.h"

typedef struct Window Window;

typedef struct VideoTexture
{
    int texture_id;
    int width;
    int height;
} VideoTexture;

int video_texture_upload(Window *window, VideoTexture *texture,
                         const VideoFrame *frame, char *error, size_t error_size);

int video_texture_close(Window *window, VideoTexture *texture);

#endif