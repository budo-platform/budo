#ifndef BUDO_APP_ENTRYPOINT_H
#define BUDO_APP_ENTRYPOINT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        APP_ENTRYPOINT_NONE = 0,
        APP_ENTRYPOINT_JAVASCRIPT = 1u << 0,
        APP_ENTRYPOINT_LUA = 1u << 1,
        APP_ENTRYPOINT_WEBASSEMBLY = 1u << 2
    } AppEntrypointRuntime;

    typedef struct
    {
        AppEntrypointRuntime runtime;
        const char *filename;
    } AppEntrypoint;

    bool app_entrypoint_resolve(const char *root, unsigned allowed_runtimes,
                                AppEntrypoint *out, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif