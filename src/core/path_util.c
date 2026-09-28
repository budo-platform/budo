#include "core/path_util.h"

#include <stddef.h>
#include <string.h>

bool path_is_safe_relative(const char *path)
{
    const char *segment;

    if (!path || !path[0])
        return false;

    if (path[0] == '/' || path[0] == '\\')
        return false;

    if (strstr(path, ":") != NULL)
        return false;

    segment = path;
    while (*segment)
    {
        const char *next = segment;
        size_t len;

        while (*next && *next != '/' && *next != '\\')
            next++;

        len = (size_t)(next - segment);
        if (len == 0 || (len == 1 && segment[0] == '.') ||
            (len == 2 && segment[0] == '.' && segment[1] == '.'))
        {
            return false;
        }

        segment = *next ? next + 1 : next;
    }

    return true;
}