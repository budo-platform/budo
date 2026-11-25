#ifndef BUDO_VIDEO_READER_H
#define BUDO_VIDEO_READER_H

#include "video/video_demux.h"
#include "video/video_frame.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct FileContext FileContext;
typedef struct VideoReader VideoReader;

typedef enum VideoReaderEventType
{
    VIDEO_READER_READY,
    VIDEO_READER_FRAME,
    VIDEO_READER_EOF,
    VIDEO_READER_ERROR
} VideoReaderEventType;

typedef struct VideoReaderEvent
{
    VideoReaderEventType type;
    VideoTrack track;
    int64_t track_duration_us;
    uint64_t ordinal;
    int64_t pts_us;
    int64_t duration_us;
    VideoOwnedFrame frame;
    char code[32];
    char message[256];
} VideoReaderEvent;

VideoReader *video_reader_open_file(FileContext *files, const char *path,
                                    char *error, size_t error_size);

bool video_reader_request_frame(VideoReader *reader, uint64_t ordinal,
                                char *error, size_t error_size);

bool video_reader_cancel_frame(VideoReader *reader);
bool video_reader_poll(VideoReader *reader, VideoReaderEvent *event);
void video_reader_event_free(VideoReaderEvent *event);
void video_reader_request_close(VideoReader *reader);
bool video_reader_stopped(VideoReader *reader);
void video_reader_close(VideoReader *reader);

#endif