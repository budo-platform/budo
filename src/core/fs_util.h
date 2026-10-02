#ifndef BUDO_FS_UTIL_H
#define BUDO_FS_UTIL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool fs_make_temp_dir(const char *prefix, char *out, size_t out_size);

    bool fs_remove_tree(const char *path);

#ifdef __cplusplus
}
#endif

#endif