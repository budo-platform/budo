#include "network/wasm_network_security.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
    assert(budo_wasm_memory_range_valid(0, 0, 0));
    assert(budo_wasm_memory_range_valid(0, 16, 16));
    assert(budo_wasm_memory_range_valid(16, 0, 16));
    assert(!budo_wasm_memory_range_valid(-1, 0, 16));
    assert(!budo_wasm_memory_range_valid(0, -1, 16));
    assert(!budo_wasm_memory_range_valid(15, 2, 16));
    assert(!budo_wasm_memory_range_valid(INT32_MAX, INT32_MAX, 65536));
    assert(budo_wasm_memory_range_valid(INT32_MAX, 0, (size_t)INT32_MAX));

    uint32_t released_token = budo_wasm_network_token(41, 0);
    uint32_t reused_token = budo_wasm_network_token(42, 0);

    assert(!budo_wasm_network_completion_matches(true, false, 42, 11,
                                                  released_token, 10));
    assert(budo_wasm_network_completion_matches(true, false, 42, 11,
                                                 reused_token, 11));
    
    assert(!budo_wasm_network_completion_matches(true, true, 42, 11,
                                                  reused_token, 11));

    puts("WASM network security tests passed");
    return 0;
}