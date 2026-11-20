#include "graphics/image_async.h"
#include "file/file_wrapper.h"
#include "web/web_image.h"

#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_INPUT_BUDGET (64u * 1024u * 1024u)

typedef struct WebImageJob
{
    uint32_t request;
    FileContext *files;
    FileNativeReference reference;
    bool opened;
    char path[FILE_MAX_PATH];
    uint8_t *bytes;
    size_t length;
    size_t offset;
    size_t encoded_reserved;
    size_t reserved;
    bool submitted;
    bool metadata;
    bool metadata_sent;
    bool decoding;
    ImageAsyncOptions options;
    ImageAsyncInfo info;
    const char *code;
} WebImageJob;

struct ImageAsync
{
    FileContext *files;
    int bridge;
    uint32_t next_request;
    size_t input_bytes;
    size_t pixel_bytes;
    WebImageJob jobs[8];
    const char *error;
};

EM_JS(bool, budo_web_image_available, (void), {
    return Module['budoImageBackend'].available();
});

EM_JS(int, bridge_create, (void), { return Module['budoImageBackend'].create(); });
EM_JS(void, bridge_destroy, (int handle), { Module['budoImageBackend'].destroy(handle); });
EM_JS(int, bridge_submit, (int handle, uint32_t request, const uint8_t *bytes, size_t length, int width, int height, size_t pixels), {
    return Module['budoImageBackend'].submit(handle, request, bytes, length, width, height, pixels);
});
EM_JS(int, bridge_status, (int handle, uint32_t request, ImageAsyncInfo *info), {
    return Module['budoImageBackend'].status(handle, request, info);
});
EM_JS(void, bridge_decode, (int handle, uint32_t request), {
    Module['budoImageBackend'].decode(handle, request);
});
EM_JS(int, bridge_copy, (int handle, uint32_t request, uint8_t *pixels, size_t length), {
    return Module['budoImageBackend'].copy(handle, request, pixels, length);
});
EM_JS(void, bridge_cancel, (int handle, uint32_t request), {
    Module['budoImageBackend'].cancel(handle, request);
});

static void release_job(ImageAsync *service, WebImageJob *job)
{
    if (job->submitted)
        bridge_cancel(service->bridge, job->request);
    if (job->opened)
        file_native_close(&job->reference);
    file_destroy(job->files);
    free(job->bytes);
    service->input_bytes -= job->encoded_reserved;
    service->pixel_bytes -= job->reserved;
    memset(job, 0, sizeof(*job));
}

ImageAsync *image_async_create(FileContext *files)
{
    ImageAsync *service = calloc(1, sizeof(*service));
    if (!service)
        return NULL;
    service->bridge = bridge_create();
    if (!service->bridge)
    {
        free(service);
        return NULL;
    }
    service->files = files;
    service->next_request = 1;
    return service;
}

void image_async_destroy(ImageAsync *service)
{
    if (!service)
        return;
    for (int index = 0; index < 8; index++)
        release_job(service, &service->jobs[index]);
    bridge_destroy(service->bridge);
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
    WebImageJob *job = NULL;
    for (int index = 0; index < 8; index++)
        if (!service->jobs[index].request)
        {
            job = &service->jobs[index];
            break;
        }
    if (!job || !service->next_request || (!path && length > (IMAGE_INPUT_BUDGET - service->input_bytes) / 2))
    {
        service->error = "Image request capacity exceeded";
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
            job->length = job->offset = length;
            job->encoded_reserved = length * 2;
            service->input_bytes += job->encoded_reserved;
        }
    }
    if ((path && !job->files) || (!path && !job->bytes))
    {
        service->error = "Cannot retain image source";
        release_job(service, job);
        return 0;
    }
    job->options = normalized;
    job->request = service->next_request++;
    return job->request;
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
    for (int index = 0; index < 8; index++)
        if (service->jobs[index].request == request)
        {
            release_job(service, &service->jobs[index]);
            return true;
        }
    return false;
}

static void read_block(ImageAsync *service, WebImageJob *job)
{
    if (!job->opened)
    {
        if (!file_native_open(job->files, job->path, &job->reference))
        {
            job->code = "image.read_failed";
            return;
        }
        job->opened = true;
        uint64_t length = job->reference.size;
        if (!length || length > job->options.max_bytes)
            job->code = "image.byte_limit";
        else if (length > (IMAGE_INPUT_BUDGET - service->input_bytes) / 2)
            job->code = "image.capacity";
        else
        {
            job->bytes = malloc((size_t)length);
            if (!job->bytes)
                job->code = "image.out_of_memory";
            else
            {
                job->length = (size_t)length;
                job->encoded_reserved = job->length * 2;
                service->input_bytes += job->encoded_reserved;
            }
        }
    }
    if (job->code)
        return;
    size_t block = job->length - job->offset;
    if (block > 65536)
        block = 65536;
    int64_t got = file_native_read(&job->reference, job->offset, job->bytes + job->offset, block);
    if (got <= 0)
        job->code = "image.read_failed";
    else
        job->offset += (size_t)got;
    if (job->offset == job->length)
    {
        file_native_close(&job->reference);
        job->opened = false;
        file_destroy(job->files);
        job->files = NULL;
    }
}

bool image_async_poll(ImageAsync *service, ImageAsyncEvent *event)
{
    if (!service || !event)
        return false;
    bool read = false;
    for (int index = 0; index < 8; index++)
    {
        WebImageJob *job = &service->jobs[index];
        if (!job->request)
            continue;
        if (job->files && !job->code && !read)
        {
            read_block(service, job);
            read = true;
        }
        if (job->bytes && job->offset == job->length && !job->submitted && !job->code)
        {
            job->submitted = bridge_submit(service->bridge, job->request, job->bytes, job->length,
                                           job->options.max_width, job->options.max_height, job->options.max_pixels);
            if (job->submitted)
            {
                free(job->bytes);
                job->bytes = NULL;
            }
        }
        int state = job->submitted ? bridge_status(service->bridge, job->request, &job->info) : 0;
        if (state == 1)
            job->metadata = true;
        if (state >= 3)
        {
            const char *codes[] = {"image.invalid_data", "image.pixel_limit", "image.out_of_memory", "image.decode_failed"};
            job->code = codes[state - 3];
        }
        memset(event, 0, sizeof(*event));
        event->request = job->request;
        event->info = job->info;
        if (job->metadata && !job->metadata_sent)
        {
            job->metadata_sent = true;
            event->type = IMAGE_ASYNC_METADATA;
            return true;
        }
        size_t output_bytes = (size_t)job->info.width * job->info.height * 4;
        if (job->metadata_sent && !job->decoding && !job->code)
        {
            size_t source_bytes = (size_t)job->info.source_width * job->info.source_height * 4;
            size_t reserve = source_bytes + output_bytes;
            if (reserve <= IMAGE_PIXEL_BUDGET - service->pixel_bytes)
            {
                job->reserved = reserve;
                service->pixel_bytes += reserve;
                job->decoding = true;
                bridge_decode(service->bridge, job->request);
            }
        }
        if (state == 2 && !job->code)
        {
            event->pixels = malloc(output_bytes);
            if (!event->pixels)
                job->code = "image.out_of_memory";
            else if (!bridge_copy(service->bridge, job->request, event->pixels, output_bytes))
            {
                free(event->pixels);
                event->pixels = NULL;
                job->code = "image.decode_failed";
            }
            else
            {
                job->submitted = false;
                event->owner = service;
                event->reserved = job->reserved;
                job->reserved = 0;
            }
        }
        if (job->code || event->pixels)
        {
            event->type = job->code ? IMAGE_ASYNC_ERROR : IMAGE_ASYNC_COMPLETE;
            event->code = job->code;
            if (job->code)
                snprintf(event->message, sizeof(event->message), "Browser image operation failed (%s)", job->code);
            release_job(service, job);
            return true;
        }
    }
    return false;
}

void image_async_event_free(ImageAsyncEvent *event)
{
    if (!event)
        return;
    free(event->pixels);
    if (event->owner)
        event->owner->pixel_bytes -= event->reserved;
    memset(event, 0, sizeof(*event));
}

bool image_async_pending(ImageAsync *service)
{
    if (service)
        for (int index = 0; index < 8; index++)
            if (service->jobs[index].request)
                return true;
    return false;
}

const char *image_async_error(ImageAsync *service)
{
    return service && service->error ? service->error : "Browser image service unavailable";
}