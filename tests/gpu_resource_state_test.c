#include "graphics/gpu_resource_state.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    GpuBufferSlot buffers[3];
    GpuTextureSlot textures[2];

    memset(buffers, 0, sizeof(buffers));
    memset(textures, 0, sizeof(textures));
    buffers[0].in_use = true;
    buffers[0].id = 41;
    buffers[2].in_use = true;
    buffers[2].id = 43;
    textures[1].in_use = true;
    textures[1].id = 52;

    assert(gpu_resource_slot_lookup(buffers, sizeof(buffers[0]), 3, 0) == NULL);
    assert(gpu_resource_slot_lookup(buffers, sizeof(buffers[0]), 3, -1) == NULL);
    assert(gpu_resource_slot_lookup(buffers, sizeof(buffers[0]), 3, 4) == NULL);
    assert(gpu_resource_slot_lookup(NULL, sizeof(buffers[0]), 3, 1) == NULL);
    assert(gpu_resource_slot_lookup(buffers, 0, 3, 1) == NULL);
    assert(gpu_resource_slot_lookup(buffers, sizeof(buffers[0]), 3, 2) == NULL);
    assert(gpu_resource_slot_lookup(buffers, sizeof(buffers[0]), 3, 1) == &buffers[0]);
    assert(gpu_resource_slot_lookup_const(buffers, sizeof(buffers[0]), 3, 3) == &buffers[2]);
    assert(gpu_resource_slot_lookup(textures, sizeof(textures[0]), 2, 2) == &textures[1]);

    buffers[0].in_use = false;
    assert(gpu_resource_slot_lookup(buffers, sizeof(buffers[0]), 3, 1) == NULL);
    buffers[0].in_use = true;
    buffers[0].id = 99;
    assert(((GpuBufferSlot *)gpu_resource_slot_lookup(
                buffers, sizeof(buffers[0]), 3, 1))
               ->id == 99);

    puts("GPU resource state tests passed");
    return 0;
}