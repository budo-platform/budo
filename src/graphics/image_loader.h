#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

   uint8_t *image_load_rgba8(const char *path, int *out_w, int *out_h);

   uint8_t *image_load_rgba8_from_memory(const uint8_t *bytes, size_t len,
                                         int *out_w, int *out_h);

   uint8_t *image_load_rgba8_scaled_from_memory(const uint8_t *bytes, size_t len,
                                                int target_width, int target_height,
                                                int *out_w, int *out_h);

   uint8_t *image_load_rgba8_scaled_cancellable(const uint8_t *bytes, size_t len,
                                                int target_width, int target_height,
                                                int *out_w, int *out_h,
                                                bool (*cancelled)(void *), void *opaque);

   void image_free(uint8_t *pixels);

   bool image_probe(const uint8_t *bytes, size_t length, int *width, int *height);

   void image_resize_rgba8_row(const uint8_t *source, int source_width, int source_height,
                               uint8_t *output, int width, int height, int row);

   const char *image_loader_last_error(void);

#ifdef __cplusplus
}
#endif

#endif