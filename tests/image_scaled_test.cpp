#include "graphics/image_async.h"
#include "graphics/image_loader.h"

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/encode/SkJpegEncoder.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static ImageAsyncEvent await_event(ImageAsync *service)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    ImageAsyncEvent event{};
    while (!image_async_poll(service, &event))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    return event;
}

static bool never_cancel(void *)
{
    return false;
}

static bool cancel_after_batch(void *opaque)
{
    int *checks = static_cast<int *>(opaque);
    return ++*checks >= 3;
}

extern "C" void image_scaled_test(void)
{
    const int source_width = 131, source_height = 97;
    std::vector<uint8_t> source((size_t)source_width * source_height * 4);
    for (size_t offset = 0; offset < source.size(); offset += 4)
    {
        source[offset] = 190;
        source[offset + 1] = 70;
        source[offset + 2] = 30;
        source[offset + 3] = 255;
    }
    SkImageInfo info = SkImageInfo::Make(source_width, source_height, kRGBA_8888_SkColorType, kOpaque_SkAlphaType);
    SkPixmap pixmap(info, source.data(), info.minRowBytes());
    auto jpeg = SkJpegEncoder::Encode(pixmap, {});
    assert(jpeg);
    const auto *bytes = static_cast<const uint8_t *>(jpeg->data());
    const int cases[][4] = {{1, 1, 17, 13}, {16, 12, 17, 13}, {17, 13, 17, 13}, {18, 14, 33, 25}, {34, 26, 66, 49}, {67, 50, 131, 97}, {131, 97, 131, 97}, {262, 194, 131, 97}};
    for (const auto &dimensions : cases)
    {
        int width = 0, height = 0;
        uint8_t *pixels = image_load_rgba8_scaled_from_memory(bytes, jpeg->size(), dimensions[0], dimensions[1], &width, &height);
        assert(pixels && width == dimensions[2] && height == dimensions[3]);
        int cancellable_width = 0, cancellable_height = 0;
        uint8_t *cancellable = image_load_rgba8_scaled_cancellable(bytes, jpeg->size(), dimensions[0], dimensions[1],
                                                                   &cancellable_width, &cancellable_height, never_cancel, nullptr);
        assert(cancellable && cancellable_width == width && cancellable_height == height);
        assert(std::memcmp(cancellable, pixels, (size_t)width * height * 4) == 0);
        image_free(cancellable);
        for (size_t offset = 0; offset < (size_t)width * height * 4; offset += 4)
        {
            assert(std::abs((int)pixels[offset] - 190) <= 3);
            assert(std::abs((int)pixels[offset + 1] - 70) <= 3);
            assert(std::abs((int)pixels[offset + 2] - 30) <= 3);
            assert(pixels[offset + 3] == 255);
        }
        if (width == source_width && height == source_height)
        {
            int original_width = 0, original_height = 0;
            uint8_t *original = image_load_rgba8_from_memory(bytes, jpeg->size(), &original_width, &original_height);
            assert(original && original_width == width && original_height == height);
            assert(std::memcmp(original, pixels, (size_t)width * height * 4) == 0);
            image_free(original);
        }
        image_free(pixels);
    }
    int width = -1, height = -1;
    assert(!image_load_rgba8_scaled_from_memory(nullptr, jpeg->size(), 1, 1, &width, &height));
    assert(!image_load_rgba8_scaled_from_memory(bytes, jpeg->size(), 0, 1, &width, &height));
    assert(!image_load_rgba8_scaled_from_memory(bytes, 10, 1, 1, &width, &height));
    assert(width == -1 && height == -1);
    int checks = 0;
    assert(!image_load_rgba8_scaled_cancellable(bytes, jpeg->size(), 18, 14,
                                                &width, &height, cancel_after_batch, &checks));
    assert(checks == 3 && width == -1 && height == -1);

    ImageAsync *service = image_async_create(nullptr);
    assert(service);
    ImageAsyncOptions options{};
    options.max_width = 16;
    options.max_height = 16;
    uint32_t request = image_async_load_buffer(service, bytes, jpeg->size(), &options);
    assert(request);
    ImageAsyncEvent metadata = await_event(service);
    assert(metadata.request == request && metadata.type == IMAGE_ASYNC_METADATA);
    assert(metadata.info.source_width == source_width && metadata.info.source_height == source_height);
    assert(metadata.info.width == 16 && metadata.info.height == 11);
    image_async_event_free(&metadata);
    ImageAsyncEvent complete = await_event(service);
    assert(complete.request == request && complete.type == IMAGE_ASYNC_COMPLETE && complete.pixels);
    assert(complete.info.width == 16 && complete.info.height == 11);
    assert(complete.info.source_width == source_width && complete.info.source_height == source_height);
    image_async_event_free(&complete);
    options.max_pixels = 16 * 16;
    request = image_async_load_buffer(service, bytes, jpeg->size(), &options);
    assert(request);
    ImageAsyncEvent limited = await_event(service);
    assert(limited.request == request && limited.type == IMAGE_ASYNC_ERROR);
    assert(std::strcmp(limited.code, "image.pixel_limit") == 0);
    image_async_event_free(&limited);
    image_async_destroy(service);
}