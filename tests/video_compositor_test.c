#include "video/video_compositor.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static void pixel(const uint8_t *rgba, int width, int x, int y,
                  const uint8_t expected[4])
{
    assert(memcmp(rgba + ((size_t)y * (size_t)width + (size_t)x) * 4u,
                  expected, 4u) == 0);
}

int main(void)
{
    const uint8_t background_pixels[] = {
        1, 2, 3, 255, 4, 5, 6, 255,
        7, 8, 9, 255, 10, 11, 12, 255,
    };
    const uint8_t inset_pixels[] = {
        100, 101, 102, 255, 103, 104, 105, 255,
    };
    VideoFrame background = {
        background_pixels, sizeof(background_pixels), 2, 2, 8, 0, 0, 1, 1
    };
    VideoFrame inset = {
        inset_pixels, sizeof(inset_pixels), 2, 1, 8, 0, 0, 1, 1
    };
    VideoCompositionConfig config = {4, 3, 1, 1, 2, 1};
    uint8_t output[4 * 3 * 4];
    char error[128];

    assert(video_compose_rgba(&background, &inset, &config, output,
                              sizeof(output), error, sizeof(error)));
    const uint8_t top_left[] = {1, 2, 3, 255};
    const uint8_t top_right[] = {4, 5, 6, 255};
    const uint8_t inset_left[] = {100, 101, 102, 255};
    const uint8_t inset_right[] = {103, 104, 105, 255};
    pixel(output, 4, 0, 0, top_left);
    pixel(output, 4, 3, 0, top_right);
    pixel(output, 4, 1, 1, inset_left);
    pixel(output, 4, 2, 1, inset_right);
    assert(error[0] == '\0');

    assert(!video_compose_rgba(&background, &inset, &config, output,
                               sizeof(output) - 1, error, sizeof(error)));
    assert(error[0] != '\0');
    config.inset_x = 3;
    assert(!video_compose_rgba(&background, &inset, &config, output,
                               sizeof(output), error, sizeof(error)));
    assert(error[0] != '\0');
    return 0;
}