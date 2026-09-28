#ifndef BUDO_GPU_RESOURCE_STATE_H
#define BUDO_GPU_RESOURCE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct GpuRenderTargetSlot
{
    bool in_use;
    int width;
    int height;
    bool has_depth;
    uint32_t fbo;
    uint32_t texture;
    uint32_t depth_rbo;
} GpuRenderTargetSlot;

typedef struct GpuBufferSlot
{
    bool in_use;
    uint32_t id;
    uint32_t target;
    size_t size;
} GpuBufferSlot;

typedef struct GpuTextureSlot
{
    bool in_use;
    uint32_t id;
    uint32_t target;
    int width;
    int height;
    uint32_t format;
} GpuTextureSlot;

void *gpu_resource_slot_lookup(void *slots, size_t slot_size,
                               size_t capacity, int resource_id);
const void *gpu_resource_slot_lookup_const(const void *slots, size_t slot_size,
                                           size_t capacity, int resource_id);

#ifdef __cplusplus
}
#endif

#endif