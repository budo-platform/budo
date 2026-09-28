#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "graphics/image_loader.h"

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

void image_free(uint8_t *pixels)
{
    if (pixels)
        stbi_image_free(pixels);
}

const char *image_loader_last_error(void)
{
    return stbi_failure_reason();
}