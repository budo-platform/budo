#ifndef BUDO_IMAGE_ASYNC_H
#define BUDO_IMAGE_ASYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IMAGE_MAX_PIXELS 256000000u
#define IMAGE_MAX_SOURCE_DIMENSION 32768
#define IMAGE_PIXEL_BUDGET ((size_t)2048 * 1024u * 1024u)

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct FileContext FileContext;
    typedef struct ImageAsync ImageAsync;

    typedef struct ImageAsyncOptions
    {
        int max_width;
        int max_height;
        size_t max_bytes;
        size_t max_pixels;
    } ImageAsyncOptions;

    typedef struct ImageAsyncInfo
    {
        int width;
        int height;
        int source_width;
        int source_height;
    } ImageAsyncInfo;

    typedef enum ImageAsyncEventType
    {
        IMAGE_ASYNC_METADATA,
        IMAGE_ASYNC_COMPLETE,
        IMAGE_ASYNC_ERROR
    } ImageAsyncEventType;

    typedef struct ImageAsyncEvent
    {
        uint32_t request;
        ImageAsyncEventType type;
        ImageAsyncInfo info;
        uint8_t *pixels;
        const char *code;
        char message[256];
        ImageAsync *owner;
        size_t reserved;
    } ImageAsyncEvent;

    ImageAsync *image_async_create(FileContext *files);
    void image_async_destroy(ImageAsync *service);
    uint32_t image_async_load_file(ImageAsync *service, const char *path,
                                   const ImageAsyncOptions *options);
    uint32_t image_async_load_buffer(ImageAsync *service, const uint8_t *bytes, size_t length,
                                     const ImageAsyncOptions *options);
    bool image_async_cancel(ImageAsync *service, uint32_t request);
    bool image_async_poll(ImageAsync *service, ImageAsyncEvent *event);
    void image_async_event_free(ImageAsyncEvent *event);
    bool image_async_pending(ImageAsync *service);
    const char *image_async_error(ImageAsync *service);

#ifdef __cplusplus
}
#endif

#endif