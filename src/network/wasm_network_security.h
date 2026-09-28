#ifndef BUDO_WASM_NETWORK_SECURITY_H
#define BUDO_WASM_NETWORK_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BUDO_WASM_NETWORK_SLOT_BITS 5u
#define BUDO_WASM_NETWORK_SLOT_MASK 31u
#define BUDO_WASM_NETWORK_GENERATION_MASK 0x07ffffffu

static inline bool budo_wasm_memory_range_valid(int32_t pointer, int32_t length,
                                                size_t memory_size)
{
    if (pointer < 0 || length < 0)
        return false;
    size_t offset = (size_t)pointer;
    size_t extent = (size_t)length;
    return offset <= memory_size && extent <= memory_size - offset;
}

static inline uint32_t budo_wasm_network_token(uint32_t generation, uint32_t slot)
{
    return (generation << BUDO_WASM_NETWORK_SLOT_BITS) |
           (slot & BUDO_WASM_NETWORK_SLOT_MASK);
}

static inline uint32_t budo_wasm_network_token_generation(uint32_t token)
{
    return token >> BUDO_WASM_NETWORK_SLOT_BITS;
}

static inline uint32_t budo_wasm_network_token_slot(uint32_t token)
{
    return token & BUDO_WASM_NETWORK_SLOT_MASK;
}

static inline bool budo_wasm_network_completion_matches(bool in_use, bool ready,
                                                        uint32_t generation,
                                                        int async_id,
                                                        uint32_t token,
                                                        int request_id)
{
    return in_use && !ready &&
           generation == budo_wasm_network_token_generation(token) &&
           async_id == request_id;
}

#endif