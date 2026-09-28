#include "gpu_resource_state.h"

#include <stdint.h>

void *gpu_resource_slot_lookup(void *slots, size_t slot_size,
                               size_t capacity, int resource_id)
{
    uint8_t *slot;

    if (!slots || slot_size < sizeof(bool) || resource_id <= 0 ||
        (size_t)resource_id > capacity)
        return NULL;
    slot = (uint8_t *)slots + ((size_t)resource_id - 1) * slot_size;
    return *(const bool *)slot ? slot : NULL;
}

const void *gpu_resource_slot_lookup_const(const void *slots, size_t slot_size,
                                           size_t capacity, int resource_id)
{
    return gpu_resource_slot_lookup((void *)slots, slot_size, capacity,
                                    resource_id);
}