#ifndef BUDO_VIDEO_FRAME_H
#define BUDO_VIDEO_FRAME_H

#include "video/video_demux.h"

typedef struct VideoOwnedFrame
{
    VideoFrame frame;
    uint8_t *pixels;
} VideoOwnedFrame;

#define VIDEO_OWNED_FRAME_INIT \
    {                           \
        {0}, NULL               \
    }

int video_owned_frame_copy(VideoOwnedFrame *destination,
                           const VideoFrame *source,
                           char *error, size_t error_size);
void video_owned_frame_init(VideoOwnedFrame *frame);
void video_owned_frame_release(VideoOwnedFrame *frame);

#endif