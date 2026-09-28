#ifndef BUDO_NATIVE_SDK_H
#define BUDO_NATIVE_SDK_H

#include <stdbool.h>
#include <stddef.h>

#include "native/native_cache.h"

#define BUDO_NATIVE_SDK_ID_SIZE 65

typedef struct NativeSdkSelection
{
    char directory[BUDO_NATIVE_CACHE_PATH_SIZE];
    char manifest_sha256[BUDO_NATIVE_SDK_ID_SIZE];
    char release_metadata_sha256[BUDO_NATIVE_SDK_ID_SIZE];
    char archive_sha256[BUDO_NATIVE_SDK_ID_SIZE];
    char sdk_input_digest[BUDO_NATIVE_SDK_ID_SIZE];
    char target_tuple[128];
    char compiler_family[128];
    char source[32];
    bool downloaded;
} NativeSdkSelection;

bool native_sdk_use_directory(const char *directory, const char *source,
                              NativeSdkSelection *selection,
                              char *error, size_t error_size);

bool native_sdk_resolve(bool offline, NativeSdkSelection *selection,
                        char *error, size_t error_size);

int native_sdk_cache_inspect(bool json);
int native_sdk_cache_clean_all(void);

bool native_sdk_extract_tar_gz(const char *archive, const char *destination,
                               char *error, size_t error_size);
bool native_sdk_verify_directory(const char *directory,
                                 NativeSdkSelection *selection,
                                 char *error, size_t error_size);

#endif