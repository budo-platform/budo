#ifndef BUDO_NATIVE_CACHE_H
#define BUDO_NATIVE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BUDO_NATIVE_CACHE_DIGEST_SIZE 32
#define BUDO_NATIVE_CACHE_HEX_SIZE 65
#define BUDO_NATIVE_CACHE_PATH_SIZE 1024

typedef struct NativeCacheHash
{
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char block[64];
    size_t block_size;
} NativeCacheHash;

typedef struct NativeCacheLock
{
#ifdef _WIN32
    void *handle;
#else
    int descriptor;
#endif
} NativeCacheLock;

void native_cache_hash_init(NativeCacheHash *hash);
void native_cache_hash_bytes(NativeCacheHash *hash, const void *data, size_t size);
void native_cache_hash_component(NativeCacheHash *hash, const char *name,
                                 const void *data, size_t size);
bool native_cache_hash_file_component(NativeCacheHash *hash, const char *name,
                                      const char *logical_path,
                                      const char *file_path);
bool native_cache_hash_tree_component(NativeCacheHash *hash, const char *name,
                                      const char *logical_root,
                                      const char *directory);
bool native_cache_hash_header_tree_component(NativeCacheHash *hash,
                                             const char *logical_root,
                                             const char *directory);
void native_cache_hash_finish(NativeCacheHash *hash,
                              char output[BUDO_NATIVE_CACHE_HEX_SIZE]);
bool native_cache_file_digest(const char *path,
                              char output[BUDO_NATIVE_CACHE_HEX_SIZE]);

bool native_cache_root(char *path, size_t size);
bool native_cache_make_directories(const char *path);
bool native_cache_lock_acquire(const char *path, NativeCacheLock *lock);
void native_cache_lock_release(NativeCacheLock *lock);

int native_cache_lookup(const char *key, const char *destination,
                        char output_digest[BUDO_NATIVE_CACHE_HEX_SIZE]);
bool native_cache_publish(const char *key, const char *executable,
                          const char *output_digest, const char *record_json);
bool native_cache_remove(const char *key);

int native_cache_inspect(bool json);
int native_cache_clean_all(void);

#endif