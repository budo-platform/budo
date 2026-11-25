#include "video/video_export.h"

#include "core/platform_thread.h"
#include "file/file_wrapper.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_EXPORT_MAX_PIXELS (16u * 1024u * 1024u)

struct VideoExport
{
    BudoMutex mutex;
    BudoThread worker;
    FileContext *files;
    char background_path[FILE_MAX_PATH];
    char inset_path[FILE_MAX_PATH];
    char output_path[FILE_MAX_PATH];
    VideoCompositionConfig config;
    int video_bit_rate;
    bool cancelled;
    bool finalizing;
    VideoExportEvent event;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

static bool valid_config(const VideoCompositionConfig *config)
{
    if (!config || config->output_width <= 0 || config->output_height <= 0 ||
        config->output_width > INT_MAX / 4 ||
        (uint64_t)config->output_width * (uint64_t)config->output_height >
            VIDEO_EXPORT_MAX_PIXELS ||
        config->inset_x < 0 || config->inset_y < 0 ||
        config->inset_width <= 0 || config->inset_height <= 0 ||
        config->inset_width > config->output_width ||
        config->inset_height > config->output_height)
        return false;
    return config->inset_x <= config->output_width - config->inset_width &&
           config->inset_y <= config->output_height - config->inset_height;
}

static bool export_is_cancelled(void *opaque)
{
    VideoExport *export = opaque;
    budo_mutex_lock(&export->mutex);
    bool cancelled = export->cancelled;
    budo_mutex_unlock(&export->mutex);
    return cancelled;
}

static bool export_begin_finalization(void *opaque)
{
    VideoExport *export = opaque;
    budo_mutex_lock(&export->mutex);
    bool accepted = !export->cancelled;
    if (accepted)
        export->finalizing = true;
    budo_mutex_unlock(&export->mutex);
    return accepted;
}

static void export_progress(void *opaque, int64_t completed_frames)
{
    VideoExport *export = opaque;
    budo_mutex_lock(&export->mutex);
    export->event.completed_frames = completed_frames;
    budo_mutex_unlock(&export->mutex);
}

static BUDO_THREAD_RETURN export_worker(void *opaque)
{
    VideoExport *export = opaque;
    char error[256] = {0};
    VideoCompositionExportControl control = {
        .is_cancelled = export_is_cancelled,
        .begin_finalization = export_begin_finalization,
        .progress = export_progress,
        .context = export,
    };
    int success = video_composition_export_file_controlled(
        export->files, export->background_path, export->inset_path,
        export->output_path, &export->config, export->video_bit_rate,
        &control, error, sizeof(error));
    file_destroy(export->files);
    export->files = NULL;

    budo_mutex_lock(&export->mutex);
    if (success)
        export->event.status = VIDEO_EXPORT_SUCCEEDED;
    else if (strcmp(error, "Video export was cancelled") == 0)
    {
        export->event.status = VIDEO_EXPORT_CANCELLED;
        snprintf(export->event.code, sizeof(export->event.code),
                 "%s", "video.cancelled");
    }
    else
    {
        export->event.status = VIDEO_EXPORT_FAILED;
        snprintf(export->event.code, sizeof(export->event.code),
                 "%s", "video.export_failed");
    }
    snprintf(export->event.message, sizeof(export->event.message), "%s",
             error);
    budo_mutex_unlock(&export->mutex);
    return BUDO_THREAD_RESULT;
}

VideoExport *video_export_start_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const char *output_path, const VideoCompositionConfig *config,
    int video_bit_rate, char *error, size_t error_size)
{
    if (!files || !background_path || !inset_path || !output_path ||
        strlen(background_path) >= FILE_MAX_PATH ||
        strlen(inset_path) >= FILE_MAX_PATH ||
        strlen(output_path) >= FILE_MAX_PATH || video_bit_rate <= 0 ||
        !valid_config(config) ||
        !file_virtual_path_is_valid(background_path, false, false) ||
        !file_virtual_path_is_valid(inset_path, false, false) ||
        !file_virtual_path_is_valid(output_path, false, true))
    {
        set_error(error, error_size, "Invalid video export arguments");
        return NULL;
    }

    VideoExport *export = calloc(1, sizeof(*export));
    if (!export)
    {
        set_error(error, error_size, "Cannot allocate video export");
        return NULL;
    }
    if (!budo_mutex_init(&export->mutex))
    {
        free(export);
        set_error(error, error_size, "Cannot initialize video export lock");
        return NULL;
    }
    export->files = file_clone(files);
    if (!export->files)
    {
        budo_mutex_destroy(&export->mutex);
        free(export);
        set_error(error, error_size, "Cannot retain video file context");
        return NULL;
    }

    snprintf(export->background_path, sizeof(export->background_path), "%s",
             background_path);
    snprintf(export->inset_path, sizeof(export->inset_path), "%s",
             inset_path);
    snprintf(export->output_path, sizeof(export->output_path), "%s",
             output_path);
    export->config = *config;
    export->video_bit_rate = video_bit_rate;
    export->event.status = VIDEO_EXPORT_RUNNING;
    if (!budo_thread_create(&export->worker, export_worker, export))
    {
        file_destroy(export->files);
        budo_mutex_destroy(&export->mutex);
        free(export);
        set_error(error, error_size, "Cannot start video export worker");
        return NULL;
    }
    set_error(error, error_size, "");
    return export;
}

bool video_export_poll(VideoExport *export, VideoExportEvent *event)
{
    if (!export || !event)
        return false;
    budo_mutex_lock(&export->mutex);
    *event = export->event;
    budo_mutex_unlock(&export->mutex);
    return true;
}

bool video_export_cancel(VideoExport *export)
{
    if (!export)
        return false;
    budo_mutex_lock(&export->mutex);
    bool accepted = export->event.status == VIDEO_EXPORT_RUNNING &&
                    !export->cancelled && !export->finalizing;
    if (accepted)
        export->cancelled = true;
    budo_mutex_unlock(&export->mutex);
    return accepted;
}

void video_export_close(VideoExport *export)
{
    if (!export)
        return;
    video_export_cancel(export);
    budo_thread_join(export->worker);
    file_destroy(export->files);
    budo_mutex_destroy(&export->mutex);
    free(export);
}