#include "graphics/image_upload.h"

#include <string.h>

void image_upload_start(ImageUpload *upload, ImageAsyncEvent *event)
{
    upload->event = *event;
    memset(event, 0, sizeof(*event));
}

bool image_upload_poll(ImageUpload *upload, Window *window, ImageAsyncEvent *result, int *texture)
{
    ImageAsyncEvent *event = &upload->event;
    if (!event->request)
        return false;
    if (!upload->texture)
        upload->texture = window_gl_create_texture_2d(window, event->info.width,
                                                      event->info.height, WINDOW_GL_TEX_RGBA8, NULL);
    int rows = (4 * 1024 * 1024) / (event->info.width * 4);
    if (rows > event->info.height - upload->row)
        rows = event->info.height - upload->row;
    bool success = upload->texture > 0 && window_gl_update_texture_2d(window, upload->texture,
        0, upload->row, event->info.width, rows, event->pixels + (size_t)upload->row * event->info.width * 4);
    upload->row += rows;
    if (success && upload->row != event->info.height)
        return false;
    memset(result, 0, sizeof(*result));
    result->request = event->request;
    result->info = event->info;
    result->type = success ? IMAGE_ASYNC_COMPLETE : IMAGE_ASYNC_ERROR;
    if (!success)
    {
        if (upload->texture > 0)
            window_gl_destroy_texture(window, upload->texture);
        result->code = "image.upload_failed";
        strcpy(result->message, "Cannot allocate or upload image texture");
    }
    *texture = success ? upload->texture : 0;
    upload->texture = 0;
    upload->row = 0;
    image_async_event_free(event);
    return true;
}

void image_upload_cancel(ImageUpload *upload, Window *window)
{
    if (upload->texture > 0)
        window_gl_destroy_texture(window, upload->texture);
    image_async_event_free(&upload->event);
    upload->texture = 0;
    upload->row = 0;
}