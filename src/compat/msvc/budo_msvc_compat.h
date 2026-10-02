#ifndef BUDO_MSVC_COMPAT_H
#define BUDO_MSVC_COMPAT_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

static __inline char *budo_compat_realpath(const char *path, char *resolved)
{
    struct _stat64 info;
    char *out = resolved ? resolved : (char *)malloc(PATH_MAX);

    if (!out)
    {
        errno = ENOMEM;
        return NULL;
    }
    if (!path || !_fullpath(out, path, PATH_MAX) || _stat64(out, &info) != 0)
    {
        if (!resolved)
            free(out);
        errno = ENOENT;
        return NULL;
    }
    return out;
}
#define realpath(path, resolved) budo_compat_realpath((path), (resolved))

#endif