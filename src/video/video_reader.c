#include "video/video_reader.h"

#include "core/platform_thread.h"
#include "file/file_wrapper.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/mathematics.h>

struct VideoReader
{
    BudoMutex mutex;
    BudoCondition changed;
    BudoThread worker;
    FileContext *files;
    char path[FILE_MAX_PATH];
    VideoDemux *demux;
    VideoTrack track;
    int64_t track_duration_us;
    int track_index;
    int64_t last_ordinal;
    bool ready;
    bool request_pending;
    bool operation_active;
    bool cancel_requested;
    uint64_t requested_ordinal;
    bool event_ready;
    VideoReaderEvent event;
    bool stopping;
    bool worker_done;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

void video_reader_event_free(VideoReaderEvent *event)
{
    if (!event)
        return;
    video_owned_frame_release(&event->frame);
    memset(event, 0, sizeof(*event));
}

static void publish_event(VideoReader *reader, VideoReaderEvent *event)
{
    budo_mutex_lock(&reader->mutex);
    while (reader->event_ready && !reader->stopping)
        budo_condition_wait(&reader->changed, &reader->mutex);
    if (!reader->stopping)
    {
        reader->event = *event;
        memset(event, 0, sizeof(*event));
        reader->event_ready = true;
        budo_condition_broadcast(&reader->changed);
    }
    budo_mutex_unlock(&reader->mutex);
    video_reader_event_free(event);
}

static bool supported_track(const VideoTrack *track, char *error,
                            size_t error_size)
{
    const uint32_t required = VIDEO_TRACK_HAS_START_TIME |
                              VIDEO_TRACK_HAS_DURATION |
                              VIDEO_TRACK_HAS_DIMENSIONS |
                              VIDEO_TRACK_HAS_FRAME_RATE |
                              VIDEO_TRACK_HAS_SAMPLE_ASPECT_RATIO |
                              VIDEO_TRACK_HAS_ROTATION;
    if (track->type != VIDEO_TRACK_VIDEO ||
        (track->metadata & required) != required ||
        strcmp(track->codec, "h264") != 0 ||
        track->time_base_num <= 0 || track->time_base_den <= 0 ||
        track->duration <= 0 ||
        track->width <= 0 || track->height <= 0 ||
        track->width > INT_MAX / 4 ||
        (uint64_t)track->width * (uint64_t)track->height >
            16u * 1024u * 1024u)
    {
        set_error(error, error_size,
                  "Video reader requires a bounded H.264 track with known metadata");
        return false;
    }
    int64_t duration_us = av_rescale_q_rnd(
        track->duration,
        (AVRational){track->time_base_num, track->time_base_den},
        (AVRational){1, 1000000},
        AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    if (duration_us <= 0 || duration_us > INT64_C(9007199254740991))
    {
        set_error(error, error_size,
                  "Video duration is outside the supported timestamp range");
        return false;
    }
    if (track->frame_rate_num <= 0 || track->frame_rate_den <= 0 ||
        av_cmp_q((AVRational){track->frame_rate_num, track->frame_rate_den},
                 (AVRational){30, 1}) != 0)
    {
        set_error(error, error_size, "Video reader requires nominal 30 fps input");
        return false;
    }
    if (track->sample_aspect_ratio_num <= 0 ||
        track->sample_aspect_ratio_den <= 0 ||
        av_cmp_q((AVRational){track->sample_aspect_ratio_num,
                             track->sample_aspect_ratio_den},
                 (AVRational){1, 1}) != 0 ||
        track->rotation_degrees != 0)
    {
        set_error(error, error_size,
                  "Video reader requires square pixels and no display rotation");
        return false;
    }
    if (track->color_primaries != VIDEO_COLOR_BT709 ||
        track->color_transfer != VIDEO_COLOR_BT709 ||
        track->color_matrix != VIDEO_COLOR_BT709 ||
        (track->color_range != VIDEO_COLOR_RANGE_FULL &&
         track->color_range != VIDEO_COLOR_RANGE_LIMITED))
    {
        set_error(error, error_size,
                  "Video reader requires explicitly tagged BT.709 SDR color");
        return false;
    }
    return true;
}

static int first_video_track(VideoDemux *demux, VideoTrack *track,
                             char *error, size_t error_size)
{
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        if (video_demux_track(demux, i, track) &&
            track->type == VIDEO_TRACK_VIDEO)
            return i;
    }
    set_error(error, error_size, "Video input has no video track");
    return -1;
}

static bool validate_frame(VideoReader *reader, const VideoFrame *frame,
                           uint64_t ordinal, int64_t *pts_us,
                           int64_t *duration_us, char *error,
                           size_t error_size)
{
    if (ordinal > INT64_MAX)
    {
        set_error(error, error_size, "Video frame ordinal is too large");
        return false;
    }
    AVRational time_base = {reader->track.time_base_num,
                            reader->track.time_base_den};
    int64_t expected_offset = av_rescale_q_rnd(
        (int64_t)ordinal, (AVRational){1, 30}, time_base,
        AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    if (expected_offset == INT64_MIN ||
        expected_offset == INT64_MAX ||
        reader->track.start_time > INT64_MAX - expected_offset)
    {
        set_error(error, error_size, "Video frame ordinal or timestamp overflows");
        return false;
    }
    int64_t expected_pts = reader->track.start_time + expected_offset;
    if (frame->pts < reader->track.start_time)
    {
        set_error(error, error_size,
                  "Decoded frame precedes the video stream start time");
        return false;
    }
    if (!frame->rgba || frame->pts == INT64_MIN ||
        frame->width != reader->track.width ||
        frame->height != reader->track.height ||
        frame->time_base_num != reader->track.time_base_num ||
        frame->time_base_den != reader->track.time_base_den ||
        frame->pts != expected_pts || frame->interlaced ||
        frame->sample_aspect_ratio_num <= 0 ||
        frame->sample_aspect_ratio_den <= 0 ||
        av_cmp_q((AVRational){frame->sample_aspect_ratio_num,
                             frame->sample_aspect_ratio_den},
                 (AVRational){1, 1}) != 0 ||
        frame->color_primaries != VIDEO_COLOR_BT709 ||
        frame->color_transfer != VIDEO_COLOR_BT709 ||
        frame->color_matrix != VIDEO_COLOR_BT709 ||
        (frame->color_range != VIDEO_COLOR_RANGE_FULL &&
         frame->color_range != VIDEO_COLOR_RANGE_LIMITED))
    {
        set_error(error, error_size,
                  "Decoded frame does not match the supported 30 fps BT.709 track");
        return false;
    }
    *pts_us = av_rescale_q(expected_offset,
                           time_base, (AVRational){1, 1000000});
    *duration_us = frame->duration > 0
                       ? av_rescale_q(frame->duration, time_base,
                                      (AVRational){1, 1000000})
                       : 0;
    if (*pts_us < 0 || *pts_us > INT64_C(9007199254740991) ||
        *duration_us < 0 || *duration_us > INT64_C(9007199254740991))
    {
        set_error(error, error_size,
                  "Decoded frame timestamp is outside JavaScript's safe integer range");
        return false;
    }
    return true;
}

static bool reader_is_interrupted(VideoReader *reader)
{
    budo_mutex_lock(&reader->mutex);
    bool interrupted = reader->stopping || reader->cancel_requested;
    budo_mutex_unlock(&reader->mutex);
    return interrupted;
}

static int decode_requested_frame(VideoReader *reader, uint64_t ordinal,
                                  VideoReaderEvent *event, char *error,
                                  size_t error_size)
{
    VideoFrame frame = {0};
    int status;
    int64_t previous_ordinal = reader->last_ordinal;
    reader->last_ordinal = -1;
    uint64_t current = 0;
    if (previous_ordinal >= 0 &&
        ordinal > (uint64_t)previous_ordinal)
    {
        current = (uint64_t)previous_ordinal + 1;
    }
    else if (!video_demux_decode_rewind(reader->demux, error, error_size))
        return -1;
    for (;;)
    {
        if (reader_is_interrupted(reader))
        {
            set_error(error, error_size, "Video reader is closing");
            return -1;
        }
        status = video_demux_decode_next(reader->demux, &frame,
                                         error, error_size);
        if (status != 1)
            return status;
        if (frame.pts == INT64_MIN)
        {
            set_error(error, error_size,
                      "Cannot seek by frame when a decoded frame has no timestamp");
            return -2;
        }
        if (frame.pts < reader->track.start_time)
            continue;
        int64_t pts_us;
        int64_t duration_us;
        if (!validate_frame(reader, &frame, current, &pts_us, &duration_us,
                            error, error_size))
            return -2;
        if (current == ordinal)
        {
            event->pts_us = pts_us;
            event->duration_us = duration_us;
            if (!video_owned_frame_copy(&event->frame, &frame,
                                        error, error_size))
                return -2;
            reader->last_ordinal = (int64_t)ordinal;
            return 1;
        }
        if (current == UINT64_MAX)
        {
            set_error(error, error_size,
                      "Decoded frame ordinal exceeds supported range");
            return -1;
        }
        current++;
    }
}

static BUDO_THREAD_RETURN reader_worker(void *opaque)
{
    VideoReader *reader = opaque;
    char error[256] = {0};
    reader->demux = video_demux_open_file(reader->files, reader->path,
                                          error, sizeof(error));
    if (reader->demux)
    {
        reader->track_index = first_video_track(reader->demux, &reader->track,
                                                error, sizeof(error));
        if (reader->track_index < 0 ||
            !supported_track(&reader->track, error, sizeof(error)) ||
            !video_demux_decode_open(reader->demux, reader->track_index,
                                     error, sizeof(error)))
        {
            video_demux_close(reader->demux);
            reader->demux = NULL;
        }
        else
        {
            reader->track_duration_us = av_rescale_q_rnd(
                reader->track.duration,
                (AVRational){reader->track.time_base_num,
                             reader->track.time_base_den},
                (AVRational){1, 1000000},
                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        }
    }

    if (!reader->demux)
    {
        VideoReaderEvent event = {.type = VIDEO_READER_ERROR};
        snprintf(event.code, sizeof(event.code), "%s", "video.open_failed");
        snprintf(event.message, sizeof(event.message), "%s",
                 error[0] ? error : "Cannot open video reader");
        publish_event(reader, &event);
        goto done;
    }

    VideoReaderEvent ready = {
        .type = VIDEO_READER_READY,
        .track = reader->track,
        .track_duration_us = reader->track_duration_us,
    };
    publish_event(reader, &ready);

    budo_mutex_lock(&reader->mutex);
    reader->ready = true;
    budo_condition_broadcast(&reader->changed);
    for (;;)
    {
        while (!reader->stopping && !reader->request_pending)
            budo_condition_wait(&reader->changed, &reader->mutex);
        if (reader->stopping)
        {
            budo_mutex_unlock(&reader->mutex);
            break;
        }
        uint64_t ordinal = reader->requested_ordinal;
        reader->request_pending = false;
        reader->operation_active = true;
        budo_mutex_unlock(&reader->mutex);

        VideoReaderEvent event = {.ordinal = ordinal};
        error[0] = '\0';
        int status = decode_requested_frame(reader, ordinal, &event,
                                            error, sizeof(error));
        if (status == 0)
            event.type = VIDEO_READER_EOF;
        else if (status < 0)
        {
            event.type = VIDEO_READER_ERROR;
            snprintf(event.code, sizeof(event.code), "%s",
                     status == -2 ? "video.frame_unsupported"
                                  : "video.decode_failed");
            snprintf(event.message, sizeof(event.message), "%s",
                     error[0] ? error : "Cannot decode requested frame");
        }
        else
        {
            event.type = VIDEO_READER_FRAME;
        }
        publish_event(reader, &event);
        budo_mutex_lock(&reader->mutex);
    }
    video_demux_close(reader->demux);
    reader->demux = NULL;

done:
    file_destroy(reader->files);
    reader->files = NULL;
    budo_mutex_lock(&reader->mutex);
    reader->worker_done = true;
    budo_condition_broadcast(&reader->changed);
    budo_mutex_unlock(&reader->mutex);
    return BUDO_THREAD_RESULT;
}

VideoReader *video_reader_open_file(FileContext *files, const char *path,
                                    char *error, size_t error_size)
{
    if (!files || !path || strlen(path) >= FILE_MAX_PATH ||
        !file_virtual_path_is_valid(path, false, false) ||
        (strncmp(path, "assets/", 7) != 0 &&
         strncmp(path, "files/", 6) != 0))
    {
        set_error(error, error_size, "Invalid sandboxed video path");
        return NULL;
    }
    VideoReader *reader = calloc(1, sizeof(*reader));
    if (!reader)
    {
        set_error(error, error_size, "Cannot allocate video reader");
        return NULL;
    }
    if (!budo_mutex_init(&reader->mutex))
    {
        free(reader);
        set_error(error, error_size, "Cannot initialize video reader lock");
        return NULL;
    }
    if (!budo_condition_init(&reader->changed))
    {
        budo_mutex_destroy(&reader->mutex);
        free(reader);
        set_error(error, error_size, "Cannot initialize video reader queue");
        return NULL;
    }
    reader->files = file_clone(files);
    reader->last_ordinal = -1;
    snprintf(reader->path, sizeof(reader->path), "%s", path);
    if (!reader->files ||
        !budo_thread_create(&reader->worker, reader_worker, reader))
    {
        file_destroy(reader->files);
        budo_condition_destroy(&reader->changed);
        budo_mutex_destroy(&reader->mutex);
        free(reader);
        set_error(error, error_size, "Cannot start video reader worker");
        return NULL;
    }
    set_error(error, error_size, "");
    return reader;
}

bool video_reader_request_frame(VideoReader *reader, uint64_t ordinal,
                                char *error, size_t error_size)
{
    if (!reader || ordinal > INT64_MAX)
    {
        set_error(error, error_size, "Invalid video frame ordinal");
        return false;
    }
    budo_mutex_lock(&reader->mutex);
    bool accepted = reader->ready && !reader->stopping &&
                    !reader->request_pending && !reader->operation_active &&
                    !reader->event_ready;
    if (accepted)
    {
        reader->requested_ordinal = ordinal;
        reader->request_pending = true;
        budo_condition_broadcast(&reader->changed);
    }
    budo_mutex_unlock(&reader->mutex);
    set_error(error, error_size, accepted ? "" :
              "Video reader is busy, not ready, or closing");
    return accepted;
}

bool video_reader_cancel_frame(VideoReader *reader)
{
    if (!reader)
        return false;
    budo_mutex_lock(&reader->mutex);
    bool accepted = !reader->stopping && !reader->cancel_requested &&
                    (reader->request_pending || reader->operation_active);
    if (accepted)
    {
        reader->cancel_requested = true;
        budo_condition_broadcast(&reader->changed);
    }
    budo_mutex_unlock(&reader->mutex);
    return accepted;
}

bool video_reader_poll(VideoReader *reader, VideoReaderEvent *event)
{
    if (!reader || !event)
        return false;
    memset(event, 0, sizeof(*event));
    budo_mutex_lock(&reader->mutex);
    bool available = reader->event_ready;
    if (available)
    {
        *event = reader->event;
        memset(&reader->event, 0, sizeof(reader->event));
        reader->event_ready = false;
        if (event->type == VIDEO_READER_READY)
            reader->ready = true;
        else
        {
            if (reader->cancel_requested)
            {
                video_reader_event_free(event);
                event->type = VIDEO_READER_ERROR;
                snprintf(event->code, sizeof(event->code), "%s", "video.cancelled");
                snprintf(event->message, sizeof(event->message), "%s",
                         "Video frame request was cancelled");
            }
            reader->cancel_requested = false;
            reader->operation_active = false;
        }
        budo_condition_broadcast(&reader->changed);
    }
    budo_mutex_unlock(&reader->mutex);
    return available;
}

void video_reader_request_close(VideoReader *reader)
{
    if (!reader)
        return;
    budo_mutex_lock(&reader->mutex);
    reader->stopping = true;
    budo_condition_broadcast(&reader->changed);
    budo_mutex_unlock(&reader->mutex);
}

bool video_reader_stopped(VideoReader *reader)
{
    if (!reader)
        return true;
    budo_mutex_lock(&reader->mutex);
    bool stopped = reader->worker_done;
    budo_mutex_unlock(&reader->mutex);
    return stopped;
}

void video_reader_close(VideoReader *reader)
{
    if (!reader)
        return;
    video_reader_request_close(reader);
    budo_thread_join(reader->worker);
    video_reader_event_free(&reader->event);
    video_demux_close(reader->demux);
    file_destroy(reader->files);
    budo_condition_destroy(&reader->changed);
    budo_mutex_destroy(&reader->mutex);
    free(reader);
}