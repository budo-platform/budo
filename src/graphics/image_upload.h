#ifndef BUDO_IMAGE_UPLOAD_H
#define BUDO_IMAGE_UPLOAD_H

#include "graphics/image_async.h"
#include "core/window.h"

typedef struct ImageUpload
{
    ImageAsyncEvent event;
    int texture;
    int row;
} ImageUpload;

void image_upload_start(ImageUpload *upload, ImageAsyncEvent *event);
bool image_upload_poll(ImageUpload *upload, Window *window, ImageAsyncEvent *result, int *texture);
void image_upload_cancel(ImageUpload *upload, Window *window);

#endif