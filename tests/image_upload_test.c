#include "graphics/image_upload.h"
#include "graphics/image_scheduler.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int created;
static int updated;
static int destroyed;
static int freed;
static bool fail_update;
static ImageAsyncEvent worker_event;
static uint32_t next_request;

ImageAsync *image_async_create(FileContext *files)
{
    (void)files;
    next_request = 1;
    memset(&worker_event, 0, sizeof(worker_event));
    return (ImageAsync *)&next_request;
}

void image_async_destroy(ImageAsync *service) { (void)service; }
uint32_t image_async_load_file(ImageAsync *service, const char *path, const ImageAsyncOptions *options)
{
    (void)service; (void)path; (void)options;
    return next_request++;
}
uint32_t image_async_load_buffer(ImageAsync *service, const uint8_t *bytes, size_t length, const ImageAsyncOptions *options)
{
    (void)bytes; (void)length;
    return image_async_load_file(service, NULL, options);
}
bool image_async_cancel(ImageAsync *service, uint32_t request)
{
    (void)service; (void)request;
    return true;
}
bool image_async_poll(ImageAsync *service, ImageAsyncEvent *event)
{
    (void)service;
    if (!worker_event.request)
        return false;
    *event = worker_event;
    memset(&worker_event, 0, sizeof(worker_event));
    return true;
}
bool image_async_pending(ImageAsync *service) { (void)service; return worker_event.request != 0; }
const char *image_async_error(ImageAsync *service) { (void)service; return "worker error"; }

int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    (void)window; (void)width; (void)height; (void)format; (void)pixels;
    created++;
    return 17;
}

bool window_gl_update_texture_2d(Window *window, int texture, int x, int y,
                                 int width, int height, const void *pixels)
{
    (void)window; (void)x; (void)pixels;
    assert(texture == 17);
    assert((size_t)width * height * 4 <= 4u * 1024u * 1024u);
    assert(y == updated * 1024);
    updated++;
    return !fail_update;
}

bool window_gl_destroy_texture(Window *window, int texture)
{
    (void)window;
    assert(texture == 17);
    destroyed++;
    return true;
}

void image_async_event_free(ImageAsyncEvent *event)
{
    if (event->pixels)
        freed++;
    free(event->pixels);
    memset(event, 0, sizeof(*event));
}

static void start(ImageUpload *upload, uint32_t request)
{
    ImageAsyncEvent event = {0};
    event.request = request;
    event.type = IMAGE_ASYNC_COMPLETE;
    event.info.width = 1024;
    event.info.height = 2049;
    event.pixels = calloc((size_t)event.info.width * event.info.height, 4);
    assert(event.pixels);
    image_upload_start(upload, &event);
    assert(!event.request && !event.pixels);
}

int main(void)
{
    ImageUpload upload = {0};
    ImageAsyncEvent result;
    int texture = 0;
    start(&upload, 1);
    assert(!image_upload_poll(&upload, NULL, &result, &texture));
    assert(!image_upload_poll(&upload, NULL, &result, &texture));
    assert(image_upload_poll(&upload, NULL, &result, &texture));
    assert(result.request == 1 && result.type == IMAGE_ASYNC_COMPLETE && texture == 17);
    assert(created == 1 && updated == 3 && destroyed == 0 && freed == 1);

    updated = 0;
    start(&upload, 2);
    assert(!image_upload_poll(&upload, NULL, &result, &texture));
    image_upload_cancel(&upload, NULL);
    assert(destroyed == 1 && freed == 2 && !upload.event.request);

    updated = 0;
    fail_update = true;
    start(&upload, 3);
    assert(image_upload_poll(&upload, NULL, &result, &texture));
    assert(result.request == 3 && result.type == IMAGE_ASYNC_ERROR && !texture);
    assert(strcmp(result.code, "image.upload_failed") == 0);
    assert(destroyed == 2 && freed == 3);

    ImageScheduler *scheduler = image_scheduler_create(NULL);
    assert(scheduler);
    for (uint32_t request = 1; request <= 8; request++)
        assert(image_scheduler_load_file(scheduler, "image", NULL) == request);
    assert(!image_scheduler_load_file(scheduler, "image", NULL));
    worker_event.request = 1;
    worker_event.type = IMAGE_ASYNC_METADATA;
    image_scheduler_begin_poll(scheduler);
    assert(image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(result.request == 1 && result.type == IMAGE_ASYNC_METADATA);
    assert(image_scheduler_cancel(scheduler, NULL, 1));
    assert(!image_scheduler_cancel(scheduler, NULL, 1));
    assert(image_scheduler_load_file(scheduler, "image", NULL) == 9);
    worker_event.request = 1;
    worker_event.type = IMAGE_ASYNC_METADATA;
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));

    worker_event.request = 2;
    worker_event.type = IMAGE_ASYNC_COMPLETE;
    worker_event.info.width = 1024;
    worker_event.info.height = 2049;
    worker_event.pixels = calloc((size_t)1024 * 2049, 4);
    assert(worker_event.pixels);
    fail_update = false;
    updated = 0;
    image_scheduler_begin_poll(scheduler);
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(updated == 1 && image_scheduler_pending(scheduler));
    image_scheduler_begin_poll(scheduler);
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));
    image_scheduler_begin_poll(scheduler);
    assert(image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(result.request == 2 && result.type == IMAGE_ASYNC_COMPLETE && texture == 17);
    assert(!image_scheduler_cancel(scheduler, NULL, 2));

    worker_event.request = 3;
    worker_event.type = IMAGE_ASYNC_COMPLETE;
    worker_event.info.width = 1024;
    worker_event.info.height = 2049;
    worker_event.pixels = calloc((size_t)1024 * 2049, 4);
    assert(worker_event.pixels);
    updated = 0;
    image_scheduler_begin_poll(scheduler);
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(updated == 1 && image_scheduler_pending(scheduler));
    assert(image_scheduler_cancel(scheduler, NULL, 3));
    assert(destroyed == 3 && freed == 5);
    assert(!image_scheduler_cancel(scheduler, NULL, 3));
    assert(image_scheduler_load_file(scheduler, "image", NULL) == 10);

    worker_event.request = 3;
    worker_event.type = IMAGE_ASYNC_COMPLETE;
    worker_event.pixels = calloc(1, 4);
    assert(worker_event.pixels);
    image_scheduler_begin_poll(scheduler);
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(freed == 6 && destroyed == 3);

    worker_event.request = 4;
    worker_event.type = IMAGE_ASYNC_COMPLETE;
    worker_event.info.width = 1024;
    worker_event.info.height = 2049;
    worker_event.pixels = calloc((size_t)1024 * 2049, 4);
    assert(worker_event.pixels);
    updated = 0;
    image_scheduler_begin_poll(scheduler);
    assert(!image_scheduler_poll(scheduler, NULL, &result, &texture));
    assert(updated == 1);
    image_scheduler_destroy(scheduler, NULL);
    assert(destroyed == 4 && freed == 7);
    return 0;
}