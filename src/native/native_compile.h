#ifndef BUDO_NATIVE_COMPILE_H
#define BUDO_NATIVE_COMPILE_H

#include <stdbool.h>

typedef enum NativeBuildConfiguration
{
    NATIVE_BUILD_RELWITHDEBINFO = 0,
    NATIVE_BUILD_DEBUG,
    NATIVE_BUILD_RELEASE
} NativeBuildConfiguration;

enum
{
    NATIVE_SANITIZER_NONE = 0,
    NATIVE_SANITIZER_ADDRESS = 1u << 0,
    NATIVE_SANITIZER_UNDEFINED = 1u << 1
};

typedef struct NativeCompileOptions
{
    const char *build_directory;
    const char *sdk_directory;
    bool run_after_build;
    bool offline;
    NativeBuildConfiguration configuration;
    unsigned sanitizers;
} NativeCompileOptions;

int native_compile_project(const char *input_path,
                           const NativeCompileOptions *options);

int native_compile_cache_inspect(bool json);
int native_compile_cache_clean_all(void);
int native_compile_sdk_cache_inspect(bool json);
int native_compile_sdk_cache_clean_all(void);

#endif