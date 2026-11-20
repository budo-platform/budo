#include "graphics/image_async.h"
#include "graphics/image_loader.h"
#include "file/file_wrapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void image_scaled_test(void);

static const uint8_t image_bytes[] = {
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};

static uint8_t reference_sample(const uint8_t *source, int source_width, int source_height,
                                int width, int height, int column, int row, int channel)
{
    double source_x = ((double)column + 0.5) * source_width / width - 0.5;
    double source_y = ((double)row + 0.5) * source_height / height - 0.5;
    if (source_x < 0)
        source_x = 0;
    if (source_y < 0)
        source_y = 0;
    int left = (int)source_x, top = (int)source_y;
    int right = left + 1 < source_width ? left + 1 : left;
    int bottom = top + 1 < source_height ? top + 1 : top;
    double horizontal = source_x - left, vertical = source_y - top;
    double upper = source[((size_t)top * source_width + left) * 4 + channel] * (1 - horizontal) +
                   source[((size_t)top * source_width + right) * 4 + channel] * horizontal;
    double lower = source[((size_t)bottom * source_width + left) * 4 + channel] * (1 - horizontal) +
                   source[((size_t)bottom * source_width + right) * 4 + channel] * horizontal;
    return (uint8_t)(upper * (1 - vertical) + lower * vertical + 0.5);
}

static void check_resize(void)
{
    const int dimensions[][2] = {{1, 1}, {1, 9}, {13, 1}, {2, 2}, {7, 11}, {17, 13}, {64, 48}, {131, 97}};
    uint8_t source[131 * 97 * 4], output[131 * 97 * 4 + 2];
    uint32_t random = 17;
    for (size_t offset = 0; offset < sizeof(source); offset++)
    {
        random = random * 1664525u + 1013904223u;
        source[offset] = (uint8_t)(random >> 24);
    }
    for (size_t input = 0; input < sizeof(dimensions) / sizeof(dimensions[0]); input++)
        for (size_t target = 0; target < sizeof(dimensions) / sizeof(dimensions[0]); target++)
        {
            int source_width = dimensions[input][0], source_height = dimensions[input][1];
            int width = dimensions[target][0], height = dimensions[target][1];
            memset(output, 0xa5, sizeof(output));
            for (int row = 0; row < height; row++)
            {
                image_resize_rgba8_row(source, source_width, source_height, output + 1, width, height, row);
                for (int column = 0; column < width; column++)
                    for (int channel = 0; channel < 4; channel++)
                    {
                        int expected = reference_sample(source, source_width, source_height, width, height, column, row, channel);
                        int actual = output[1 + ((size_t)row * width + column) * 4 + channel];
                        assert(abs(actual - expected) <= 1);
                    }
            }
            assert(output[0] == 0xa5 && output[1 + (size_t)width * height * 4] == 0xa5);
            if (input == target)
                assert(memcmp(source, output + 1, (size_t)width * height * 4) == 0);
        }
}

static ImageAsyncEvent next_event(ImageAsync *service)
{
    clock_t started = clock();
    ImageAsyncEvent event;
    while (!image_async_poll(service, &event))
        assert((double)(clock() - started) / CLOCKS_PER_SEC < 10);
    return event;
}

static void check_complete(ImageAsync *service, uint32_t request, int width)
{
    ImageAsyncEvent metadata = next_event(service);
    assert(metadata.request == request && metadata.type == IMAGE_ASYNC_METADATA);
    assert(metadata.info.source_width == 2 && metadata.info.source_height == 2);
    assert(metadata.info.width == width && metadata.info.height == width);
    image_async_event_free(&metadata);
    ImageAsyncEvent complete = next_event(service);
    assert(complete.request == request && complete.type == IMAGE_ASYNC_COMPLETE);
    assert(complete.pixels && complete.pixels[0] == 255 && complete.pixels[1] == 0 && complete.pixels[3] == 255);
    image_async_event_free(&complete);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    check_resize();
    image_scaled_test();
    FileContext *files = file_create(argv[1]);
    assert(files);
    file_set_write_root(files, argv[1]);
    ImageAsync *service = image_async_create(files);
    assert(service);
    uint8_t retained[sizeof(image_bytes)];
    memcpy(retained, image_bytes, sizeof(retained));
    ImageAsyncOptions options = {.max_width = 1, .max_height = 1};
    uint32_t request = image_async_load_buffer(service, retained, sizeof(retained), &options);
    assert(request);
    memset(retained, 0, sizeof(retained));
    check_complete(service, request, 1);
    assert(!image_async_cancel(service, request));

    assert(IMAGE_PIXEL_BUDGET >= (size_t)IMAGE_MAX_PIXELS * 4 * 2);
    options.max_pixels = 256000000;
    request = image_async_load_buffer(service, image_bytes, sizeof(image_bytes), &options);
    assert(request);
    check_complete(service, request, 1);
    options.max_pixels++;
    assert(!image_async_load_buffer(service, image_bytes, sizeof(image_bytes), &options));

    const uint8_t large_header[] = {
        137, 80, 78, 71, 13, 10, 26, 10,
        0, 0, 0, 13, 73, 72, 68, 82,
        0, 0, 78, 32, 0, 0, 46, 224,
        8, 6, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 73, 68, 65, 84, 0, 0, 0, 0,
        0, 0, 0, 0, 73, 69, 78, 68, 0, 0, 0, 0};
    request = image_async_load_buffer(service, large_header, sizeof(large_header), NULL);
    assert(request);
    ImageAsyncEvent event = next_event(service);
    assert(event.type == IMAGE_ASYNC_METADATA);
    assert(event.info.source_width == 20000 && event.info.source_height == 12000);
    image_async_event_free(&event);
    assert(image_async_cancel(service, request));
    image_async_destroy(service);
    service = image_async_create(files);
    assert(service);

    options.max_pixels = 1;
    request = image_async_load_buffer(service, image_bytes, sizeof(image_bytes), &options);
    assert(request);
    event = next_event(service);
    assert(event.type == IMAGE_ASYNC_ERROR && strcmp(event.code, "image.pixel_limit") == 0);
    image_async_event_free(&event);

    request = image_async_load_buffer(service, (const uint8_t *)"broken", 6, NULL);
    assert(request);
    event = next_event(service);
    assert(event.type == IMAGE_ASYNC_ERROR && strcmp(event.code, "image.invalid_data") == 0);
    image_async_event_free(&event);

    uint32_t queued[8];
    for (int index = 0; index < 8; index++)
    {
        queued[index] = image_async_load_buffer(service, image_bytes, sizeof(image_bytes), NULL);
        assert(queued[index]);
    }
    assert(!image_async_load_buffer(service, image_bytes, sizeof(image_bytes), NULL));
    for (int index = 0; index < 8; index++)
        assert(image_async_cancel(service, queued[index]));
    assert(!image_async_poll(service, &event));
    image_async_destroy(service);

    assert(file_write_binary(files, "files/image-async.tga", image_bytes, sizeof(image_bytes)));
    service = image_async_create(files);
    assert(service);
    assert(!image_async_load_file(service, "files/../escape.tga", NULL));
    request = image_async_load_file(service, "files/image-async.tga", NULL);
    assert(request);
    file_destroy(files);
    check_complete(service, request, 2);
    image_async_destroy(service);
    puts("image_async: metadata, resize, ownership, errors, capacity, cancellation, sandbox and teardown passed");
    return 0;
}