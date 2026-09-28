#include "native/native_cache.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "native_cache_test: failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); return 1; } } while (0)

int main(void)
{
    NativeCacheHash hash;
    char digest[BUDO_NATIVE_CACHE_HEX_SIZE];

    native_cache_hash_init(&hash);
    native_cache_hash_bytes(&hash, "abc", 3);
    native_cache_hash_finish(&hash, digest);
    CHECK(strcmp(digest,
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

    native_cache_hash_init(&hash);
    native_cache_hash_component(&hash, "a", "bc", 2);
    native_cache_hash_finish(&hash, digest);
    CHECK(strcmp(digest,
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0);

    puts("native_cache_test: ok");
    return 0;
}