#include "graphics/image_scheduler.h"
#include "graphics/image_upload.h"

#include <stdlib.h>
#include <string.h>

struct ImageScheduler
{
    ImageAsync *service;
    ImageUpload upload;
    uint32_t requests[8];
    bool upload_allowed;
    const char *error;
};

static uint32_t *find_request(ImageScheduler *scheduler, uint32_t request)
{
    for (int index = 0; index < 8; index++)
        if (scheduler->requests[index] == request)
            return &scheduler->requests[index];
    return NULL;
}

ImageScheduler *image_scheduler_create(FileContext *files)
{
    ImageScheduler *scheduler = calloc(1, sizeof(*scheduler));
    if (!scheduler)
        return NULL;
    scheduler->service = image_async_create(files);
    if (!scheduler->service)
    {
        free(scheduler);
        return NULL;
    }
    return scheduler;
}

void image_scheduler_destroy(ImageScheduler *scheduler, Window *window)
{
    if (!scheduler)
        return;
    image_upload_cancel(&scheduler->upload, window);
    image_async_destroy(scheduler->service);
    free(scheduler);
}

uint32_t image_scheduler_load_file(ImageScheduler *scheduler, const char *path, const ImageAsyncOptions *options)
{
    uint32_t *slot = find_request(scheduler, 0);
    if (!slot)
    {
        scheduler->error = "Maximum of 8 active texture loads exceeded";
        return 0;
    }
    scheduler->error = NULL;
    *slot = image_async_load_file(scheduler->service, path, options);
    return *slot;
}

uint32_t image_scheduler_load_buffer(ImageScheduler *scheduler, const uint8_t *bytes, size_t length, const ImageAsyncOptions *options)
{
    uint32_t *slot = find_request(scheduler, 0);
    if (!slot)
    {
        scheduler->error = "Maximum of 8 active texture loads exceeded";
        return 0;
    }
    scheduler->error = NULL;
    *slot = image_async_load_buffer(scheduler->service, bytes, length, options);
    return *slot;
}

bool image_scheduler_cancel(ImageScheduler *scheduler, Window *window, uint32_t request)
{
    if (!scheduler || !request)
        return false;
    uint32_t *slot = find_request(scheduler, request);
    if (!slot)
        return false;
    image_async_cancel(scheduler->service, request);
    if (scheduler->upload.event.request == request)
        image_upload_cancel(&scheduler->upload, window);
    *slot = 0;
    return true;
}

void image_scheduler_begin_poll(ImageScheduler *scheduler)
{
    scheduler->upload_allowed = true;
}

bool image_scheduler_poll(ImageScheduler *scheduler, Window *window, ImageAsyncEvent *event, int *texture)
{
    *texture = 0;
    if (!scheduler->upload.event.request)
    {
        ImageAsyncEvent next;
        while (image_async_poll(scheduler->service, &next))
        {
            if (!find_request(scheduler, next.request))
            {
                image_async_event_free(&next);
                continue;
            }
            if (next.type == IMAGE_ASYNC_COMPLETE)
            {
                image_upload_start(&scheduler->upload, &next);
                break;
            }
            *event = next;
            event->pixels = NULL;
            event->owner = NULL;
            event->reserved = 0;
            image_async_event_free(&next);
            if (event->type == IMAGE_ASYNC_ERROR)
                *find_request(scheduler, event->request) = 0;
            return true;
        }
    }
    if (!scheduler->upload.event.request || !scheduler->upload_allowed)
        return false;
    scheduler->upload_allowed = false;
    if (!image_upload_poll(&scheduler->upload, window, event, texture))
        return false;
    *find_request(scheduler, event->request) = 0;
    return true;
}

bool image_scheduler_pending(ImageScheduler *scheduler)
{
    return scheduler && (scheduler->upload.event.request || image_async_pending(scheduler->service));
}

const char *image_scheduler_error(ImageScheduler *scheduler)
{
    return scheduler->error ? scheduler->error : image_async_error(scheduler->service);
}