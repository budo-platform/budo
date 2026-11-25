#include "video/video_preview.h"

#include "core/platform_thread.h"
#include "file/file_wrapper.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_PREVIEW_QUEUE_CAPACITY 2

typedef struct VideoPreviewSlot
{
    VideoPreviewEvent event;
} VideoPreviewSlot;

struct VideoPreview
{
    BudoMutex mutex;
    BudoCondition changed;
    BudoThread worker;
    FileContext *files;
    char background_path[FILE_MAX_PATH];
    char inset_path[FILE_MAX_PATH];
    VideoCompositionConfig config;
    int64_t start_us;
    VideoPreviewSlot slots[VIDEO_PREVIEW_QUEUE_CAPACITY];
    int read_slot;
    int write_slot;
    int queued;
    bool stopping;
    bool worker_done;
    bool terminal_ready;
    bool terminal_sent;
    VideoPreviewEvent terminal;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

void video_preview_event_free(VideoPreviewEvent *event)
{
    if (!event)
        return;
    free(event->pixels);
    memset(event, 0, sizeof(*event));
}

static bool is_stopping(VideoPreview *preview)
{
    budo_mutex_lock(&preview->mutex);
    bool stopping = preview->stopping;
    budo_mutex_unlock(&preview->mutex);
    return stopping;
}

static void finish_worker(VideoPreview *preview, VideoPreviewEventType type,
                          const char *code, const char *message)
{
    budo_mutex_lock(&preview->mutex);
    if (!preview->stopping)
    {
        preview->terminal.type = type;
        if (code)
            snprintf(preview->terminal.code, sizeof(preview->terminal.code),
                     "%s", code);
        if (message)
            snprintf(preview->terminal.message,
                     sizeof(preview->terminal.message), "%s", message);
        preview->terminal_ready = true;
    }
    preview->worker_done = true;
    budo_condition_broadcast(&preview->changed);
    budo_mutex_unlock(&preview->mutex);
}

static BUDO_THREAD_RETURN preview_worker(void *opaque)
{
    VideoPreview *preview = opaque;
    char error[256] = {0};
    VideoComposition *composition = video_composition_open_files(
        preview->files, preview->background_path, preview->inset_path,
        &preview->config, error, sizeof(error));
    file_destroy(preview->files);
    preview->files = NULL;
    if (!composition)
    {
        finish_worker(preview, VIDEO_PREVIEW_ERROR, "video.open_failed", error);
        return BUDO_THREAD_RESULT;
    }

    int64_t frame_count = video_composition_frame_count(composition);
    
    if (frame_count > INT64_C(9007199254740991) / 1000000 * 30)
    {
        video_composition_close(composition);
        finish_worker(preview, VIDEO_PREVIEW_ERROR, "video.duration_unsupported",
                      "Video duration is too long");
        return BUDO_THREAD_RESULT;
    }
    int64_t duration_us = (frame_count / 30) * 1000000 +
                          ((frame_count % 30) * 1000000 + 15) / 30;
    int64_t target_us = preview->start_us < duration_us
                            ? preview->start_us : duration_us - 1;
    int64_t target_frame = (target_us / 1000000) * 30 +
                           ((target_us % 1000000) * 30 + 15) / 1000000;
    if (target_frame >= frame_count)
        target_frame = frame_count - 1;
    if (video_composition_seek_frame(composition, target_frame,
                                     error, sizeof(error)) != 1)
    {
        video_composition_close(composition);
        finish_worker(preview, VIDEO_PREVIEW_ERROR, "video.seek_failed",
                      error[0] ? error : "Cannot seek to preview frame");
        return BUDO_THREAD_RESULT;
    }

    size_t pixel_count = (size_t)preview->config.output_width *
                         (size_t)preview->config.output_height;
    size_t frame_size = pixel_count * 4u;
    for (;;)
    {
        budo_mutex_lock(&preview->mutex);
        while (!preview->stopping &&
               preview->queued == VIDEO_PREVIEW_QUEUE_CAPACITY)
            budo_condition_wait(&preview->changed, &preview->mutex);
        if (preview->stopping)
        {
            budo_mutex_unlock(&preview->mutex);
            break;
        }

        VideoPreviewSlot *slot = &preview->slots[preview->write_slot];
        slot->event.pixels = malloc(frame_size);
        if (!slot->event.pixels)
        {
            budo_mutex_unlock(&preview->mutex);
            video_composition_close(composition);
            finish_worker(preview, VIDEO_PREVIEW_ERROR,
                          "video.out_of_memory",
                          "Cannot allocate a preview frame");
            return BUDO_THREAD_RESULT;
        }
        uint8_t *pixels = slot->event.pixels;
        budo_mutex_unlock(&preview->mutex);

        int64_t pts_us = 0;
        int status = video_composition_next(composition, pixels, frame_size,
                                            &pts_us, error, sizeof(error));

        budo_mutex_lock(&preview->mutex);
        if (preview->stopping || status != 1)
        {
            free(slot->event.pixels);
            memset(&slot->event, 0, sizeof(slot->event));
            budo_mutex_unlock(&preview->mutex);
            if (status < 0 && !is_stopping(preview))
            {
                video_composition_close(composition);
                finish_worker(preview, VIDEO_PREVIEW_ERROR,
                              "video.decode_failed", error);
                return BUDO_THREAD_RESULT;
            }
            if (status == 0 && !is_stopping(preview))
            {
                video_composition_close(composition);
                finish_worker(preview, VIDEO_PREVIEW_END, NULL, NULL);
                return BUDO_THREAD_RESULT;
            }
            break;
        }

        slot->event.type = VIDEO_PREVIEW_FRAME;
        slot->event.pts_us = pts_us;
        slot->event.duration_us = duration_us;
        slot->event.frame.rgba = pixels;
        slot->event.frame.size = frame_size;
        slot->event.frame.width = preview->config.output_width;
        slot->event.frame.height = preview->config.output_height;
        slot->event.frame.stride = preview->config.output_width * 4;
        slot->event.frame.pts = pts_us;
        slot->event.frame.duration = 0;
        slot->event.frame.time_base_num = 1;
        slot->event.frame.time_base_den = 1000000;
        slot->event.frame.sample_aspect_ratio_num = 1;
        slot->event.frame.sample_aspect_ratio_den = 1;
        slot->event.frame.color_primaries = VIDEO_COLOR_BT709;
        slot->event.frame.color_transfer = VIDEO_COLOR_BT709;
        slot->event.frame.color_matrix = VIDEO_COLOR_BT709;
        slot->event.frame.color_range = VIDEO_COLOR_RANGE_FULL;
        preview->write_slot =
            (preview->write_slot + 1) % VIDEO_PREVIEW_QUEUE_CAPACITY;
        preview->queued++;
        budo_condition_broadcast(&preview->changed);
        budo_mutex_unlock(&preview->mutex);
    }

    video_composition_close(composition);
    if (is_stopping(preview))
    {
        budo_mutex_lock(&preview->mutex);
        preview->worker_done = true;
        budo_condition_broadcast(&preview->changed);
        budo_mutex_unlock(&preview->mutex);
    }
    return BUDO_THREAD_RESULT;
}

VideoPreview *video_preview_open_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, char *error, size_t error_size)
{
    return video_preview_open_files_at(files, background_path, inset_path,
                                       config, 0, error, error_size);
}

VideoPreview *video_preview_open_files_at(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, int64_t start_us,
    char *error, size_t error_size)
{
    if (!files || !background_path || !inset_path || !config || start_us < 0 ||
        strlen(background_path) >= FILE_MAX_PATH ||
        strlen(inset_path) >= FILE_MAX_PATH ||
        config->output_width <= 0 || config->output_height <= 0 ||
        config->output_width > INT_MAX / 4 ||
        (uint64_t)config->output_width * (uint64_t)config->output_height >
            16u * 1024u * 1024u ||
        config->inset_x < 0 || config->inset_y < 0 ||
        config->inset_width <= 0 || config->inset_height <= 0 ||
        config->inset_x > config->output_width - config->inset_width ||
        config->inset_y > config->output_height - config->inset_height ||
        !file_virtual_path_is_valid(background_path, false, false) ||
        !file_virtual_path_is_valid(inset_path, false, false))
    {
        set_error(error, error_size, "Invalid video preview arguments");
        return NULL;
    }

    VideoPreview *preview = calloc(1, sizeof(*preview));
    if (!preview)
    {
        set_error(error, error_size, "Cannot allocate video preview");
        return NULL;
    }
    if (!budo_mutex_init(&preview->mutex))
    {
        free(preview);
        set_error(error, error_size, "Cannot initialize video preview lock");
        return NULL;
    }
    if (!budo_condition_init(&preview->changed))
    {
        budo_mutex_destroy(&preview->mutex);
        free(preview);
        set_error(error, error_size, "Cannot initialize video preview queue");
        return NULL;
    }
    preview->files = file_clone(files);
    preview->config = *config;
    preview->start_us = start_us;
    snprintf(preview->background_path, sizeof(preview->background_path),
             "%s", background_path);
    snprintf(preview->inset_path, sizeof(preview->inset_path), "%s",
             inset_path);
    if (!preview->files)
    {
        budo_condition_destroy(&preview->changed);
        budo_mutex_destroy(&preview->mutex);
        free(preview);
        set_error(error, error_size, "Cannot retain video file context");
        return NULL;
    }
    if (!budo_thread_create(&preview->worker, preview_worker, preview))
    {
        file_destroy(preview->files);
        budo_condition_destroy(&preview->changed);
        budo_mutex_destroy(&preview->mutex);
        free(preview);
        set_error(error, error_size, "Cannot start video preview worker");
        return NULL;
    }
    set_error(error, error_size, "");
    return preview;
}

bool video_preview_poll_latest(VideoPreview *preview, int64_t media_time_us,
                               VideoPreviewEvent *event)
{
    if (!preview || !event)
        return false;
    memset(event, 0, sizeof(*event));
    VideoPreviewEvent latest = {0};
    bool have_frame = false;

    budo_mutex_lock(&preview->mutex);
    while (preview->queued)
    {
        VideoPreviewSlot *slot = &preview->slots[preview->read_slot];
        if (slot->event.pts_us > media_time_us)
            break;
        if (have_frame)
            video_preview_event_free(&latest);
        latest = slot->event;
        memset(&slot->event, 0, sizeof(slot->event));
        preview->read_slot =
            (preview->read_slot + 1) % VIDEO_PREVIEW_QUEUE_CAPACITY;
        preview->queued--;
        have_frame = true;
        budo_condition_broadcast(&preview->changed);
    }
    if (have_frame)
    {
        *event = latest;
        budo_mutex_unlock(&preview->mutex);
        return true;
    }
    if (!preview->queued && preview->terminal_ready &&
        !preview->terminal_sent)
    {
        *event = preview->terminal;
        preview->terminal_sent = true;
        budo_mutex_unlock(&preview->mutex);
        return true;
    }
    budo_mutex_unlock(&preview->mutex);
    return false;
}

bool video_preview_pending(VideoPreview *preview)
{
    if (!preview)
        return false;
    budo_mutex_lock(&preview->mutex);
    bool pending = preview->queued || !preview->worker_done ||
                   (preview->terminal_ready && !preview->terminal_sent);
    budo_mutex_unlock(&preview->mutex);
    return pending;
}

void video_preview_request_stop(VideoPreview *preview)
{
    if (!preview)
        return;
    budo_mutex_lock(&preview->mutex);
    preview->stopping = true;
    budo_condition_broadcast(&preview->changed);
    budo_mutex_unlock(&preview->mutex);
}

bool video_preview_stopped(VideoPreview *preview)
{
    if (!preview)
        return true;
    budo_mutex_lock(&preview->mutex);
    bool stopped = preview->worker_done;
    budo_mutex_unlock(&preview->mutex);
    return stopped;
}

void video_preview_close(VideoPreview *preview)
{
    if (!preview)
        return;
    video_preview_request_stop(preview);
    budo_thread_join(preview->worker);
    for (int i = 0; i < VIDEO_PREVIEW_QUEUE_CAPACITY; ++i)
        video_preview_event_free(&preview->slots[i].event);
    video_preview_event_free(&preview->terminal);
    file_destroy(preview->files);
    budo_condition_destroy(&preview->changed);
    budo_mutex_destroy(&preview->mutex);
    free(preview);
}