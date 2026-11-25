#include "video/video_frame_writer.h"
#include "core/platform_thread.h"
#include "file/file_wrapper.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VideoFrameWriter
{
    BudoMutex mutex;
    BudoCondition condition;
    BudoThread worker;
    FileContext *files;
    char path[FILE_MAX_PATH];
    VideoWriterConfig config;
    int total_frames;
    int completed_frames;
    bool ready;
    bool cancelled;
    bool finishing;
    bool finalizing;
    bool terminal;
    bool event_pending;
    uint8_t *pixels;
    float *audio;
    VideoFrameWriterEvent event;
};

static void set_error(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
}

bool video_frame_writer_valid_config(const VideoWriterConfig *config,
                                     int total_frames)
{
    return config && total_frames > 0 && total_frames <= 1000000 &&
           config->width > 0 && config->height > 0 &&
           !(config->width & 1) && !(config->height & 1) &&
           (uint64_t)config->width * config->height <= 16u * 1024u * 1024u &&
           config->frame_rate_den > 0 && config->frame_rate_den <= 10000 &&
           config->frame_rate_num >= config->frame_rate_den &&
           config->frame_rate_num <= 240000 &&
           config->frame_rate_num <= 240 * config->frame_rate_den &&
           config->video_bit_rate > 0 &&
           !config->subtitle_count && !config->subtitles &&
           ((!config->audio_sample_rate && !config->audio_channels &&
             !config->audio_bit_rate) ||
            ((config->audio_sample_rate == 44100 ||
              config->audio_sample_rate == 48000) &&
             (config->audio_channels == 1 || config->audio_channels == 2) &&
             config->audio_bit_rate > 0));
}

static int64_t audio_boundary(const VideoWriterConfig *config, int index)
{
    return ((int64_t)index * config->audio_sample_rate *
                config->frame_rate_den + config->frame_rate_num / 2) /
           config->frame_rate_num;
}

size_t video_frame_writer_audio_frames(const VideoWriterConfig *config,
                                       int index)
{
    return (size_t)(audio_boundary(config, index + 1) -
                    audio_boundary(config, index));
}

static void publish_event(VideoFrameWriter *writer,
                          VideoFrameWriterStatus status, const char *message)
{
    budo_mutex_lock(&writer->mutex);
    if (status != VIDEO_FRAME_WRITER_READY && writer->cancelled)
    {
        status = VIDEO_FRAME_WRITER_CANCELLED;
        message = "Video frame export was cancelled";
    }
    writer->ready = status == VIDEO_FRAME_WRITER_READY;
    writer->terminal = !writer->ready;
    writer->event = (VideoFrameWriterEvent){
        .status = status,
        .completed_frames = writer->completed_frames,
    };
    snprintf(writer->event.message, sizeof(writer->event.message), "%s",
             message);
    writer->event_pending = true;
    budo_mutex_unlock(&writer->mutex);
}

static BUDO_THREAD_RETURN frame_writer_worker(void *opaque)
{
    VideoFrameWriter *writer = opaque;
    char error[256] = {0};
    VideoWriter *sink = video_writer_open_file(
        writer->files, writer->path, &writer->config, error, sizeof(error));
    VideoFrameWriterStatus status = VIDEO_FRAME_WRITER_ERROR;
    if (sink)
    {
        publish_event(writer, VIDEO_FRAME_WRITER_READY, "");
        for (;;)
        {
            budo_mutex_lock(&writer->mutex);
            while (!writer->pixels && !writer->finishing && !writer->cancelled)
                budo_condition_wait(&writer->condition, &writer->mutex);
            bool cancelled = writer->cancelled;
            bool finishing = writer->finishing;
            if (finishing && !cancelled)
                writer->finalizing = true;
            uint8_t *pixels = writer->pixels;
            float *audio = writer->audio;
            writer->pixels = NULL;
            writer->audio = NULL;
            int index = writer->completed_frames;
            budo_mutex_unlock(&writer->mutex);
            if (cancelled || finishing)
            {
                free(pixels);
                free(audio);
                if (cancelled)
                {
                    status = VIDEO_FRAME_WRITER_CANCELLED;
                    snprintf(error, sizeof(error), "Video frame export was cancelled");
                }
                else if (video_writer_finish(sink, error, sizeof(error)))
                    status = VIDEO_FRAME_WRITER_COMPLETE;
                if (finishing && !cancelled)
                    sink = NULL;
                break;
            }
            int64_t pts_us = ((int64_t)index * 1000000 *
                                 writer->config.frame_rate_den +
                             writer->config.frame_rate_num / 2) /
                            writer->config.frame_rate_num;
            size_t bytes = (size_t)writer->config.width *
                           writer->config.height * 4;
            bool success = video_writer_write_frame(
                sink, pixels, bytes, pts_us, error, sizeof(error));
            if (success && writer->config.audio_channels)
            {
                int64_t audio_pts_us =
                    (audio_boundary(&writer->config, index) * 1000000 +
                     writer->config.audio_sample_rate / 2) /
                    writer->config.audio_sample_rate;
                success = video_writer_write_audio(
                    sink, audio,
                    video_frame_writer_audio_frames(&writer->config, index),
                    audio_pts_us, error, sizeof(error));
            }
            free(pixels);
            free(audio);
            if (!success)
                break;
            budo_mutex_lock(&writer->mutex);
            writer->completed_frames++;
            budo_mutex_unlock(&writer->mutex);
            publish_event(writer, VIDEO_FRAME_WRITER_READY, "");
        }
    }
    video_writer_close(sink);
    file_destroy(writer->files);
    writer->files = NULL;
    publish_event(writer, status, error);
    return BUDO_THREAD_RESULT;
}

VideoFrameWriter *video_frame_writer_open(
    FileContext *files, const char *path, const VideoWriterConfig *config,
    int total_frames, char *error, size_t error_size)
{
    if (!files || !path || strlen(path) >= FILE_MAX_PATH ||
        strncmp(path, "files/", 6) ||
        !file_virtual_path_is_valid(path, false, true) ||
        !video_frame_writer_valid_config(config, total_frames))
    {
        set_error(error, error_size, "Invalid video frame writer configuration");
        return NULL;
    }
    VideoFrameWriter *writer = calloc(1, sizeof(*writer));
    if (!writer)
    {
        set_error(error, error_size, "Cannot allocate video frame writer");
        return NULL;
    }
    if (!budo_mutex_init(&writer->mutex))
    {
        free(writer);
        set_error(error, error_size, "Cannot initialize video frame writer lock");
        return NULL;
    }
    if (!budo_condition_init(&writer->condition))
    {
        budo_mutex_destroy(&writer->mutex);
        free(writer);
        set_error(error, error_size, "Cannot initialize video frame writer condition");
        return NULL;
    }
    writer->files = file_clone(files);
    writer->config = *config;
    writer->total_frames = total_frames;
    snprintf(writer->path, sizeof(writer->path), "%s", path);
    if (!writer->files ||
        !budo_thread_create(&writer->worker, frame_writer_worker, writer))
    {
        file_destroy(writer->files);
        budo_condition_destroy(&writer->condition);
        budo_mutex_destroy(&writer->mutex);
        free(writer);
        set_error(error, error_size, "Cannot start video frame writer worker");
        return NULL;
    }
    set_error(error, error_size, "");
    return writer;
}

bool video_frame_writer_submit(VideoFrameWriter *writer, int index,
                               const uint8_t *pixels, size_t pixel_bytes,
                               const float *audio, size_t audio_values,
                               char *error, size_t error_size)
{
    if (!writer)
    {
        set_error(error, error_size, "Video frame writer is inactive");
        return false;
    }
    budo_mutex_lock(&writer->mutex);
    const char *message = NULL;
    if (!writer->ready || writer->event_pending || writer->cancelled ||
        writer->finishing || writer->terminal)
        message = "Video frame writer is not ready; wait for onReady";
    else if (index != writer->completed_frames || index >= writer->total_frames)
        message = "Video frames must be submitted in order within totalFrames";
    size_t expected_bytes = (size_t)writer->config.width *
                            writer->config.height * 4;
    size_t expected_values = index >= 0 && index < writer->total_frames
        ? video_frame_writer_audio_frames(&writer->config, index) *
              writer->config.audio_channels : 0;
    if (!message && (!pixels || pixel_bytes != expected_bytes ||
                     audio_values != expected_values ||
                     (expected_values && !audio)))
        message = "Incorrect RGBA or PCM buffer size for video frame";
    for (size_t i = 0; !message && i < expected_values; ++i)
        if (!isfinite(audio[i]) || audio[i] < -1 || audio[i] > 1)
            message = "Video PCM samples must be finite values in [-1, 1]";
    if (!message)
    {
        writer->pixels = malloc(expected_bytes);
        writer->audio = expected_values ? malloc(expected_values * sizeof(float)) : NULL;
        if (!writer->pixels || (expected_values && !writer->audio))
        {
            free(writer->pixels);
            free(writer->audio);
            writer->pixels = NULL;
            writer->audio = NULL;
            message = "Cannot copy video frame buffers";
        }
        else
        {
            memcpy(writer->pixels, pixels, expected_bytes);
            if (expected_values)
                memcpy(writer->audio, audio, expected_values * sizeof(float));
            writer->ready = false;
            budo_condition_broadcast(&writer->condition);
        }
    }
    budo_mutex_unlock(&writer->mutex);
    set_error(error, error_size, message ? message : "");
    return !message;
}

bool video_frame_writer_finish(VideoFrameWriter *writer,
                               char *error, size_t error_size)
{
    if (!writer)
    {
        set_error(error, error_size, "Video frame writer is inactive");
        return false;
    }
    budo_mutex_lock(&writer->mutex);
    bool accepted = writer->ready && !writer->event_pending &&
                    !writer->cancelled && !writer->finishing &&
                    writer->completed_frames == writer->total_frames;
    if (accepted)
    {
        writer->ready = false;
        writer->finishing = true;
        budo_condition_broadcast(&writer->condition);
    }
    budo_mutex_unlock(&writer->mutex);
    set_error(error, error_size, accepted ? "" :
              "Incomplete export or video frame writer is not ready to finish");
    return accepted;
}

bool video_frame_writer_cancel(VideoFrameWriter *writer)
{
    if (!writer)
        return false;
    budo_mutex_lock(&writer->mutex);
    bool accepted = !writer->terminal && !writer->cancelled &&
                    !writer->finalizing;
    if (accepted)
    {
        writer->cancelled = true;
        budo_condition_broadcast(&writer->condition);
    }
    budo_mutex_unlock(&writer->mutex);
    return accepted;
}

bool video_frame_writer_poll(VideoFrameWriter *writer,
                             VideoFrameWriterEvent *event)
{
    if (!writer || !event)
        return false;
    budo_mutex_lock(&writer->mutex);
    bool available = writer->event_pending;
    if (available)
    {
        *event = writer->event;
        writer->event_pending = false;
    }
    budo_mutex_unlock(&writer->mutex);
    return available;
}

void video_frame_writer_close(VideoFrameWriter *writer)
{
    if (!writer)
        return;
    video_frame_writer_cancel(writer);
    budo_thread_join(writer->worker);
    free(writer->pixels);
    free(writer->audio);
    budo_condition_destroy(&writer->condition);
    budo_mutex_destroy(&writer->mutex);
    free(writer);
}