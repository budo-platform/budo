#include "graphics/image_async.h"
#include "graphics/image_loader.h"
#include "core/platform_thread.h"
#include "file/file_wrapper.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BUDO_WEB

#define IMAGE_JOBS 8
#define IMAGE_WORKERS 2
#define IMAGE_INPUT_BUDGET (64u * 1024u * 1024u)

typedef struct ImageJob
{
    uint32_t request;
    bool working;
    bool cancelled;
    bool metadata;
    bool metadata_sent;
    bool done;
    FileContext *files;
    char path[FILE_MAX_PATH];
    uint8_t *bytes;
    size_t length;
    size_t reserved;
    ImageAsyncOptions options;
    ImageAsyncInfo info;
    uint8_t *pixels;
    const char *code;
    char message[256];
} ImageJob;

struct ImageAsync
{
    FileContext *files;
    BudoMutex mutex;
    BudoCondition changed;
    BudoThread workers[IMAGE_WORKERS];
    int worker_count;
    bool stopping;
    uint32_t next_request;
    size_t input_bytes;
    size_t pixel_bytes;
    ImageJob jobs[IMAGE_JOBS];
    const char *error;
};

static void release_job(ImageAsync *service, ImageJob *job)
{
    if (job->bytes)
    {
        service->input_bytes -= job->length;
        free(job->bytes);
    }
    service->pixel_bytes -= job->reserved;
    free(job->pixels);
    file_destroy(job->files);
    memset(job, 0, sizeof(*job));
    budo_condition_broadcast(&service->changed);
}

static bool interrupted(ImageAsync *service, ImageJob *job)
{
    budo_mutex_lock(&service->mutex);
    bool cancelled = service->stopping || job->cancelled;
    budo_mutex_unlock(&service->mutex);
    return cancelled;
}

typedef struct ImageDecodeCancellation
{
    ImageAsync *service;
    ImageJob *job;
} ImageDecodeCancellation;

static bool decode_cancelled(void *opaque)
{
    ImageDecodeCancellation *cancellation = opaque;
    return interrupted(cancellation->service, cancellation->job);
}

static void fail_job(ImageJob *job, const char *code, const char *message)
{
    job->code = code;
    snprintf(job->message, sizeof(job->message), "%s", message);
}

static bool publish_metadata(ImageAsync *service, ImageJob *job, int width, int height)
{
    if (width <= 0 || height <= 0 || width > IMAGE_MAX_SOURCE_DIMENSION || height > IMAGE_MAX_SOURCE_DIMENSION ||
        (size_t)width * height > job->options.max_pixels)
    {
        fail_job(job, "image.pixel_limit", "Image exceeds maxPixels or the 32768 pixel source dimension limit");
        return false;
    }
    double scale = 1.0;
    if (job->options.max_width && width * scale > job->options.max_width)
        scale = (double)job->options.max_width / width;
    if (job->options.max_height && height * scale > job->options.max_height)
        scale = (double)job->options.max_height / height;
    ImageAsyncInfo info = {(int)fmax(1, floor(width * scale)), (int)fmax(1, floor(height * scale)), width, height};
    budo_mutex_lock(&service->mutex);
    job->info = info;
    job->metadata = true;
    budo_mutex_unlock(&service->mutex);
    return true;
}

static bool read_file(ImageAsync *service, ImageJob *job)
{
    FileNativeReference reference;
    if (!file_native_open(job->files, job->path, &reference))
    {
        fail_job(job, "image.read_failed", file_get_error(job->files));
        return false;
    }
    if (!reference.size || reference.size > job->options.max_bytes)
    {
        file_native_close(&reference);
        fail_job(job, "image.byte_limit", "Image exceeds maxBytes or is empty");
        return false;
    }
    size_t length = (size_t)reference.size;
    budo_mutex_lock(&service->mutex);
    if (service->stopping || job->cancelled || service->input_bytes + length > IMAGE_INPUT_BUDGET)
    {
        budo_mutex_unlock(&service->mutex);
        file_native_close(&reference);
        fail_job(job, "image.capacity", "Encoded image memory budget exceeded");
        return false;
    }
    job->bytes = malloc(length);
    if (job->bytes)
    {
        job->length = length;
        service->input_bytes += length;
    }
    budo_mutex_unlock(&service->mutex);
    if (!job->bytes)
    {
        file_native_close(&reference);
        fail_job(job, "image.out_of_memory", "Cannot allocate encoded image");
        return false;
    }
    size_t offset = 0;
    while (offset < length && !interrupted(service, job))
    {
        size_t block = length - offset;
        if (block > 65536)
            block = 65536;
        int64_t got = file_native_read(&reference, offset, job->bytes + offset, block);
        if (got <= 0)
        {
            file_native_close(&reference);
            fail_job(job, "image.read_failed", "Image changed or could not be read");
            return false;
        }
        offset += (size_t)got;
        if (!job->metadata)
        {
            int width = 0, height = 0;
            if (image_probe(job->bytes, offset, &width, &height) && !publish_metadata(service, job, width, height))
            {
                file_native_close(&reference);
                return false;
            }
        }
    }
    file_native_close(&reference);
    return offset == length;
}

static uint8_t *resize_image(ImageAsync *service, ImageJob *job, const uint8_t *source,
                             int source_width, int source_height)
{
    const ImageAsyncInfo *info = &job->info;
    uint8_t *output = malloc((size_t)info->width * info->height * 4);
    if (!output)
        return NULL;
    for (int row = 0; row < info->height; row++)
    {
        if (interrupted(service, job))
        {
            free(output);
            return NULL;
        }
        image_resize_rgba8_row(source, source_width, source_height,
                               output, info->width, info->height, row);
    }
    return output;
}

static void decode_job(ImageAsync *service, ImageJob *job)
{
    if (!job->bytes && !read_file(service, job))
        return;
    int width = 0, height = 0;
    if (interrupted(service, job))
        return;
    if (!job->metadata && !image_probe(job->bytes, job->length, &width, &height))
    {
        fail_job(job, "image.invalid_data", "Unsupported or damaged image header");
        return;
    }
    if (!job->metadata && !publish_metadata(service, job, width, height))
        return;
    ImageAsyncInfo info = job->info;
    width = info.source_width;
    height = info.source_height;
    bool resize = info.width != width || info.height != height;
    size_t source_bytes = (size_t)width * height * 4;
    size_t output_bytes = (size_t)info.width * info.height * 4;
    size_t reserve = source_bytes + (resize ? output_bytes : 0);
    budo_mutex_lock(&service->mutex);
    while (!service->stopping && !job->cancelled && service->pixel_bytes + reserve > IMAGE_PIXEL_BUDGET)
        budo_condition_wait(&service->changed, &service->mutex);
    bool cancelled = service->stopping || job->cancelled;
    if (!cancelled)
    {
        service->pixel_bytes += reserve;
        job->reserved = reserve;
    }
    budo_mutex_unlock(&service->mutex);
    if (cancelled)
        return;
    ImageDecodeCancellation cancellation = {service, job};
    uint8_t *pixels = image_load_rgba8_scaled_cancellable(job->bytes, job->length,
                                                          info.width, info.height, &width, &height,
                                                          decode_cancelled, &cancellation);
    if (!pixels)
    {
        fail_job(job, "image.decode_failed", "Image decoder rejected the data");
        return;
    }
    if (width < info.width || height < info.height || width > info.source_width || height > info.source_height)
    {
        image_free(pixels);
        fail_job(job, "image.invalid_data", "Decoded dimensions differ from image metadata");
        return;
    }
    if (interrupted(service, job))
    {
        image_free(pixels);
        return;
    }
    resize = width != info.width || height != info.height;
    size_t retained = (size_t)width * height * 4 + (resize ? output_bytes : 0);
    budo_mutex_lock(&service->mutex);
    service->pixel_bytes -= job->reserved - retained;
    job->reserved = retained;
    budo_condition_broadcast(&service->changed);
    budo_mutex_unlock(&service->mutex);
    if (resize)
    {
        job->pixels = resize_image(service, job, pixels, width, height);
        image_free(pixels);
        if (!job->pixels && !interrupted(service, job))
            fail_job(job, "image.out_of_memory", "Cannot allocate resized image");
    }
    else
        job->pixels = pixels;
    budo_mutex_lock(&service->mutex);
    service->pixel_bytes -= job->reserved - (job->pixels ? output_bytes : 0);
    job->reserved = job->pixels ? output_bytes : 0;
    budo_condition_broadcast(&service->changed);
    budo_mutex_unlock(&service->mutex);
}

static BUDO_THREAD_RETURN image_worker(void *opaque)
{
    ImageAsync *service = opaque;
    budo_mutex_lock(&service->mutex);
    while (!service->stopping)
    {
        ImageJob *job = NULL;
        for (int index = 0; index < IMAGE_JOBS; index++)
        {
            ImageJob *candidate = &service->jobs[index];
            if (candidate->request && !candidate->working && !candidate->done &&
                (!job || candidate->request < job->request))
                job = candidate;
        }
        if (!job)
        {
            budo_condition_wait(&service->changed, &service->mutex);
            continue;
        }
        job->working = true;
        budo_mutex_unlock(&service->mutex);
        decode_job(service, job);
        budo_mutex_lock(&service->mutex);
        job->working = false;
        if (job->cancelled || service->stopping)
            release_job(service, job);
        else
        {
            if (job->bytes)
            {
                service->input_bytes -= job->length;
                free(job->bytes);
                job->bytes = NULL;
            }
            job->done = true;
            budo_condition_broadcast(&service->changed);
        }
    }
    budo_mutex_unlock(&service->mutex);
    return BUDO_THREAD_RESULT;
}

ImageAsync *image_async_create(FileContext *files)
{
    ImageAsync *service = calloc(1, sizeof(*service));
    if (!service)
        return NULL;
    if (!budo_mutex_init(&service->mutex))
    {
        free(service);
        return NULL;
    }
    if (!budo_condition_init(&service->changed))
    {
        budo_mutex_destroy(&service->mutex);
        free(service);
        return NULL;
    }
    service->files = files;
    service->next_request = 1;
    for (int index = 0; index < IMAGE_WORKERS; index++)
    {
        if (!budo_thread_create(&service->workers[index], image_worker, service))
        {
            image_async_destroy(service);
            return NULL;
        }
        service->worker_count++;
    }
    return service;
}

void image_async_destroy(ImageAsync *service)
{
    if (!service)
        return;
    budo_mutex_lock(&service->mutex);
    service->stopping = true;
    budo_condition_broadcast(&service->changed);
    budo_mutex_unlock(&service->mutex);
    for (int index = 0; index < service->worker_count; index++)
        budo_thread_join(service->workers[index]);
    for (int index = 0; index < IMAGE_JOBS; index++)
        release_job(service, &service->jobs[index]);
    budo_condition_destroy(&service->changed);
    budo_mutex_destroy(&service->mutex);
    free(service);
}

static uint32_t submit(ImageAsync *service, const char *path, const uint8_t *bytes, size_t length,
                       const ImageAsyncOptions *options)
{
    if (!service)
        return 0;
    ImageAsyncOptions normalized = options ? *options : (ImageAsyncOptions){0};
    if (!normalized.max_bytes)
        normalized.max_bytes = IMAGE_INPUT_BUDGET;
    if (!normalized.max_pixels)
        normalized.max_pixels = IMAGE_MAX_PIXELS;
    if (normalized.max_width < 0 || normalized.max_width > 16384 || normalized.max_height < 0 ||
        normalized.max_height > 16384 || normalized.max_bytes > IMAGE_INPUT_BUDGET ||
        normalized.max_pixels > IMAGE_MAX_PIXELS || (!path && (!bytes || !length || length > normalized.max_bytes)) ||
        (path && (!file_virtual_path_is_valid(path, false, false) || strlen(path) >= FILE_MAX_PATH)))
    {
        service->error = "Invalid image source or options";
        return 0;
    }
    budo_mutex_lock(&service->mutex);
    ImageJob *job = NULL;
    for (int index = 0; index < IMAGE_JOBS; index++)
        if (!service->jobs[index].request)
        {
            job = &service->jobs[index];
            break;
        }
    if (!job || !service->next_request || service->stopping ||
        (!path && service->input_bytes + length > IMAGE_INPUT_BUDGET))
    {
        service->error = "Image request capacity exceeded";
        budo_mutex_unlock(&service->mutex);
        return 0;
    }
    if (path)
    {
        job->files = file_clone(service->files);
        if (job->files)
            snprintf(job->path, sizeof(job->path), "%s", path);
    }
    else
    {
        job->bytes = malloc(length);
        if (job->bytes)
        {
            memcpy(job->bytes, bytes, length);
            job->length = length;
            service->input_bytes += length;
        }
    }
    if ((path && !job->files) || (!path && !job->bytes))
    {
        service->error = "Cannot retain image source";
        release_job(service, job);
        budo_mutex_unlock(&service->mutex);
        return 0;
    }
    job->options = normalized;
    job->request = service->next_request++;
    uint32_t request = job->request;
    budo_condition_broadcast(&service->changed);
    budo_mutex_unlock(&service->mutex);
    return request;
}

uint32_t image_async_load_file(ImageAsync *service, const char *path, const ImageAsyncOptions *options)
{
    return path ? submit(service, path, NULL, 0, options) : 0;
}

uint32_t image_async_load_buffer(ImageAsync *service, const uint8_t *bytes, size_t length, const ImageAsyncOptions *options)
{
    return submit(service, NULL, bytes, length, options);
}

bool image_async_cancel(ImageAsync *service, uint32_t request)
{
    if (!service || !request)
        return false;
    budo_mutex_lock(&service->mutex);
    bool found = false;
    for (int index = 0; index < IMAGE_JOBS; index++)
    {
        ImageJob *job = &service->jobs[index];
        if (job->request != request || job->cancelled)
            continue;
        found = true;
        job->cancelled = true;
        if (!job->working)
            release_job(service, job);
        break;
    }
    budo_condition_broadcast(&service->changed);
    budo_mutex_unlock(&service->mutex);
    return found;
}

bool image_async_poll(ImageAsync *service, ImageAsyncEvent *event)
{
    if (!service || !event)
        return false;
    budo_mutex_lock(&service->mutex);
    for (int index = 0; index < IMAGE_JOBS; index++)
    {
        ImageJob *job = &service->jobs[index];
        if (!job->request || job->cancelled)
            continue;
        if (job->metadata && !job->metadata_sent)
        {
            memset(event, 0, sizeof(*event));
            event->type = IMAGE_ASYNC_METADATA;
            event->request = job->request;
            event->info = job->info;
            job->metadata_sent = true;
            budo_mutex_unlock(&service->mutex);
            return true;
        }
        if (!job->done)
            continue;
        memset(event, 0, sizeof(*event));
        event->type = job->code ? IMAGE_ASYNC_ERROR : IMAGE_ASYNC_COMPLETE;
        event->request = job->request;
        event->info = job->info;
        event->pixels = job->pixels;
        event->code = job->code;
        memcpy(event->message, job->message, sizeof(event->message));
        event->owner = service;
        event->reserved = job->reserved;
        job->reserved = 0;
        job->pixels = NULL;
        release_job(service, job);
        budo_mutex_unlock(&service->mutex);
        return true;
    }
    budo_mutex_unlock(&service->mutex);
    return false;
}

void image_async_event_free(ImageAsyncEvent *event)
{
    if (!event)
        return;
    free(event->pixels);
    if (event->owner)
    {
        budo_mutex_lock(&event->owner->mutex);
        event->owner->pixel_bytes -= event->reserved;
        budo_condition_broadcast(&event->owner->changed);
        budo_mutex_unlock(&event->owner->mutex);
    }
    memset(event, 0, sizeof(*event));
}

bool image_async_pending(ImageAsync *service)
{
    if (!service)
        return false;
    budo_mutex_lock(&service->mutex);
    bool pending = false;
    for (int index = 0; index < IMAGE_JOBS; index++)
        pending |= service->jobs[index].request != 0;
    budo_mutex_unlock(&service->mutex);
    return pending;
}

const char *image_async_error(ImageAsync *service)
{
    return service && service->error ? service->error : "Image service unavailable";
}

#endif