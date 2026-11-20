#if defined(__aarch64__) && !defined(STBI_NO_SIMD) && !defined(STBI_NEON)
#define STBI_NEON
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "graphics/image_loader.h"
#include <limits.h>
#include <string.h>

bool image_probe(const uint8_t *bytes, size_t length, int *width, int *height)
{
    int channels;
    return bytes && length > 0 && length <= INT_MAX &&
           stbi_info_from_memory(bytes, (int)length, width, height, &channels) != 0;
}

uint8_t *image_load_rgba8(const char *path, int *out_w, int *out_h)
{
    int w = 0, h = 0, channels = 0;
    if (!path)
        return NULL;
    
    stbi_uc *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels)
        return NULL;
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
    return pixels;
}

uint8_t *image_load_rgba8_from_memory(const uint8_t *bytes, size_t len,
                                      int *out_w, int *out_h)
{
    int w = 0, h = 0, channels = 0;
    if (!bytes || len == 0)
        return NULL;
    stbi_uc *pixels = stbi_load_from_memory(bytes, (int)len, &w, &h, &channels, 4);
    if (!pixels)
        return NULL;
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
    return pixels;
}

void image_resize_rgba8_row(const uint8_t *source, int source_width, int source_height,
                            uint8_t *output, int width, int height, int row)
{
    uint8_t *destination = output + (size_t)row * width * 4;
    if (source_width == width && source_height == height)
    {
        memcpy(destination, source + (size_t)row * width * 4, (size_t)width * 4);
        return;
    }
    double source_y = ((double)row + 0.5) * source_height / height - 0.5;
    if (source_y < 0)
        source_y = 0;
    int top = (int)source_y;
    int bottom = top + 1 < source_height ? top + 1 : top;
    uint32_t vertical = (uint32_t)((source_y - top) * 256 + 0.5);
    const uint8_t *upper_row = source + (size_t)top * source_width * 4;
    const uint8_t *lower_row = source + (size_t)bottom * source_width * 4;
    double horizontal_scale = (double)source_width / width;
    for (int column = 0; column < width; column++)
    {
        double source_x = ((double)column + 0.5) * horizontal_scale - 0.5;
        if (source_x < 0)
            source_x = 0;
        int left = (int)source_x;
        int right = left + 1 < source_width ? left + 1 : left;
        uint32_t horizontal = (uint32_t)((source_x - left) * 256 + 0.5);
        const uint8_t *upper_left = upper_row + (size_t)left * 4;
        const uint8_t *upper_right = upper_row + (size_t)right * 4;
        const uint8_t *lower_left = lower_row + (size_t)left * 4;
        const uint8_t *lower_right = lower_row + (size_t)right * 4;
        for (int channel = 0; channel < 4; channel += 2)
        {
            uint64_t upper = (upper_left[channel] | (uint64_t)upper_left[channel + 1] << 32) * (256 - horizontal) +
                             (upper_right[channel] | (uint64_t)upper_right[channel + 1] << 32) * horizontal;
            uint64_t lower = (lower_left[channel] | (uint64_t)lower_left[channel + 1] << 32) * (256 - horizontal) +
                             (lower_right[channel] | (uint64_t)lower_right[channel + 1] << 32) * horizontal;
            uint64_t blended = (upper * (256 - vertical) + lower * vertical + UINT64_C(0x800000008000)) >> 16;
            destination[(size_t)column * 4 + channel] = (uint8_t)blended;
            destination[(size_t)column * 4 + channel + 1] = (uint8_t)(blended >> 32);
        }
    }
}

void image_free(uint8_t *pixels)
{
    if (pixels)
        stbi_image_free(pixels);
}

const char *image_loader_last_error(void)
{
    return stbi_failure_reason();
}