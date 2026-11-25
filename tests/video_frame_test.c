#include "video/video_frame.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t source_pixels[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 99, 99,
        9, 10, 11, 12, 13, 14, 15, 16, 88, 88, 88, 88,
    };
    VideoFrame source = {
        .rgba = source_pixels,
        .size = sizeof(source_pixels),
        .width = 2,
        .height = 2,
        .stride = 12,
        .pts = 25,
        .duration = 1,
        .time_base_num = 1,
        .time_base_den = 30,
    };
    VideoOwnedFrame owned = VIDEO_OWNED_FRAME_INIT;
    char error[128];

    assert(video_owned_frame_copy(&owned, &source, error, sizeof(error)));
    assert(error[0] == '\0');
    assert(owned.pixels && owned.frame.rgba == owned.pixels);
    assert(owned.frame.stride == 8 && owned.frame.size == 16);
    assert(owned.frame.pts == source.pts);
    assert(memcmp(owned.pixels, source_pixels, 8) == 0);
    assert(memcmp(owned.pixels + 8, source_pixels + 12, 8) == 0);
    source_pixels[0] = 42;
    assert(owned.pixels[0] == 1);

    VideoOwnedFrame replacement = VIDEO_OWNED_FRAME_INIT;
    source.width = 0;
    assert(!video_owned_frame_copy(&owned, &source, error, sizeof(error)));
    assert(error[0] != '\0');
    assert(owned.frame.width == 2);

    source.width = 2;
    source.size = 19;
    assert(!video_owned_frame_copy(&replacement, &source, error, sizeof(error)));
    assert(error[0] != '\0');
    source.size = sizeof(source_pixels);

    source.width = 4097;
    source.height = 4096;
    source.stride = source.width * 4;
    source.size = (size_t)source.stride * (size_t)source.height;
    assert(!video_owned_frame_copy(&replacement, &source, error, sizeof(error)));
    assert(error[0] != '\0');

    video_owned_frame_release(&owned);
    video_owned_frame_release(&owned);
    video_owned_frame_release(&replacement);
    assert(!owned.pixels && !owned.frame.rgba && owned.frame.width == 0);
    return 0;
}