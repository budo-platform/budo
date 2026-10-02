#ifndef BUDO_COMPAT_LIBGEN_H
#define BUDO_COMPAT_LIBGEN_H

#include <string.h>

#define BUDO_COMPAT_IS_SEP(c) ((c) == '/' || (c) == '\\')

static inline char *budo_compat_basename(char *path)
{
    static char dot[] = ".";
    char *end;
    char *start;

    if (!path || !path[0])
        return dot;
    end = path + strlen(path) - 1;
    while (end > path && BUDO_COMPAT_IS_SEP(*end))
        *end-- = '\0';
    start = end;
    while (start > path && !BUDO_COMPAT_IS_SEP(start[-1]) && start[-1] != ':')
        start--;
    return start;
}

static inline char *budo_compat_dirname(char *path)
{
    static char dot[] = ".";
    char *end;

    if (!path || !path[0])
        return dot;
    end = path + strlen(path) - 1;
    while (end > path && BUDO_COMPAT_IS_SEP(*end))
        end--;
    while (end > path && !BUDO_COMPAT_IS_SEP(*end))
        end--;
    if (end == path)
    {
        if (!BUDO_COMPAT_IS_SEP(*end))
            return dot;
        end[1] = '\0';
        return path;
    }
    while (end > path && BUDO_COMPAT_IS_SEP(end[-1]))
        end--;
    *end = '\0';
    return path;
}

#define basename budo_compat_basename
#define dirname budo_compat_dirname

#endif