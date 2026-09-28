#ifndef BUDO_PATH_UTIL_H
#define BUDO_PATH_UTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool path_is_safe_relative(const char *path);

#ifdef __cplusplus
}
#endif

#endif