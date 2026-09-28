#include "app_entrypoint.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool regular_file(const char *root, const char *name)
{
    char path[4096];
    struct stat status;
    int length = snprintf(path, sizeof(path), "%s/%s", root, name);
    return length > 0 && (size_t)length < sizeof(path) &&
           stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

bool app_entrypoint_resolve(const char *root, unsigned allowed_runtimes,
                            AppEntrypoint *out, char *error, size_t error_size)
{
    static const struct
    {
        const char *name;
        AppEntrypointRuntime runtime;
    } candidates[] = {
        {"main.ts", APP_ENTRYPOINT_JAVASCRIPT},
        {"main.js", APP_ENTRYPOINT_JAVASCRIPT},
        {"main.lua", APP_ENTRYPOINT_LUA},
        {"main.wasm", APP_ENTRYPOINT_WEBASSEMBLY},
        {"main.wat", APP_ENTRYPOINT_WEBASSEMBLY},
    };
    unsigned found_runtimes = 0;
    AppEntrypoint selected = {APP_ENTRYPOINT_NONE, NULL};

    if (out)
        *out = selected;
    if (error && error_size)
        error[0] = '\0';
    if (!root || !out)
        return false;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (!(allowed_runtimes & (unsigned)candidates[i].runtime) ||
            !regular_file(root, candidates[i].name))
            continue;
        found_runtimes |= (unsigned)candidates[i].runtime;
        if (selected.runtime == APP_ENTRYPOINT_NONE)
        {
            selected.runtime = candidates[i].runtime;
            selected.filename = candidates[i].name;
        }
    }

    if (found_runtimes && (found_runtimes & (found_runtimes - 1u)))
    {
        if (error && error_size)
            snprintf(error, error_size,
                     "Ambiguous managed entrypoints: files for multiple runtimes are present");
        return false;
    }
    if (selected.runtime == APP_ENTRYPOINT_NONE)
    {
        if (error && error_size)
            snprintf(error, error_size, "No supported managed entrypoint found");
        return false;
    }
    *out = selected;
    return true;
}