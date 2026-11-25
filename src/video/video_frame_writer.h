#ifndef BUDO_VIDEO_FRAME_WRITER_H
#define BUDO_VIDEO_FRAME_WRITER_H

#include "video/video_demux.h"
#include <stdbool.h>

typedef struct VideoFrameWriter VideoFrameWriter;

typedef enum
{
    VIDEO_FRAME_WRITER_READY,
    VIDEO_FRAME_WRITER_COMPLETE,
    VIDEO_FRAME_WRITER_CANCELLED,
    VIDEO_FRAME_WRITER_ERROR
} VideoFrameWriterStatus;

typedef struct
{
    VideoFrameWriterStatus status;
    int completed_frames;
    char message[256];
} VideoFrameWriterEvent;

bool video_frame_writer_valid_config(const VideoWriterConfig *config,
                                     int total_frames);

size_t video_frame_writer_audio_frames(const VideoWriterConfig *config,
                                       int index);
VideoFrameWriter *video_frame_writer_open(
    FileContext *files, const char *path, const VideoWriterConfig *config,
    int total_frames, char *error, size_t error_size);

bool video_frame_writer_submit(VideoFrameWriter *writer, int index,
                               const uint8_t *pixels, size_t pixel_bytes,
                               const float *audio, size_t audio_values,
                               char *error, size_t error_size);
bool video_frame_writer_finish(VideoFrameWriter *writer,
                               char *error, size_t error_size);
bool video_frame_writer_cancel(VideoFrameWriter *writer);
bool video_frame_writer_poll(VideoFrameWriter *writer,
                             VideoFrameWriterEvent *event);

void video_frame_writer_close(VideoFrameWriter *writer);

#endif