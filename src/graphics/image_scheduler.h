#ifndef BUDO_IMAGE_SCHEDULER_H
#define BUDO_IMAGE_SCHEDULER_H

#include "graphics/image_async.h"
#include "core/window.h"

typedef struct ImageScheduler ImageScheduler;

ImageScheduler *image_scheduler_create(FileContext *files);
void image_scheduler_destroy(ImageScheduler *scheduler, Window *window);
uint32_t image_scheduler_load_file(ImageScheduler *scheduler, const char *path, const ImageAsyncOptions *options);
uint32_t image_scheduler_load_buffer(ImageScheduler *scheduler, const uint8_t *bytes, size_t length, const ImageAsyncOptions *options);
bool image_scheduler_cancel(ImageScheduler *scheduler, Window *window, uint32_t request);
void image_scheduler_begin_poll(ImageScheduler *scheduler);
bool image_scheduler_poll(ImageScheduler *scheduler, Window *window, ImageAsyncEvent *event, int *texture);
bool image_scheduler_pending(ImageScheduler *scheduler);
const char *image_scheduler_error(ImageScheduler *scheduler);

#endif