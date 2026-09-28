#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    uint8_t *image_load_rgba8(const char *path, int *out_w, int *out_h);

    uint8_t *image_load_rgba8_from_memory(const uint8_t *bytes, size_t len,
                                          int *out_w, int *out_h);

    void image_free(uint8_t *pixels);

    const char *image_loader_last_error(void);

#ifdef __cplusplus
}
#endif

#endif