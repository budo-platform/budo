#ifndef BUDO_COMPAT_DIRENT_H
#define BUDO_COMPAT_DIRENT_H

#include <errno.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct dirent
{
    char d_name[260];
};

typedef struct DIR
{
    intptr_t handle;
    struct _finddata_t data;
    struct dirent entry;
    int pending; 
} DIR;

static inline DIR *opendir(const char *path)
{
    size_t length;
    char *pattern;
    DIR *dir;

    if (!path || !path[0])
    {
        errno = ENOENT;
        return NULL;
    }
    length = strlen(path);
    pattern = (char *)malloc(length + 3);
    dir = (DIR *)calloc(1, sizeof(*dir));
    if (!pattern || !dir)
    {
        free(pattern);
        free(dir);
        errno = ENOMEM;
        return NULL;
    }
    memcpy(pattern, path, length);
    if (path[length - 1] != '/' && path[length - 1] != '\\')
        pattern[length++] = '\\';
    pattern[length++] = '*';
    pattern[length] = '\0';

    dir->handle = _findfirst(pattern, &dir->data); 
    free(pattern);
    if (dir->handle == -1)
    {
        free(dir);
        return NULL;
    }
    dir->pending = 1;
    return dir;
}

static inline struct dirent *readdir(DIR *dir)
{
    if (!dir)
        return NULL;
    if (!dir->pending && _findnext(dir->handle, &dir->data) != 0)
        return NULL;
    dir->pending = 0;
    strncpy(dir->entry.d_name, dir->data.name, sizeof(dir->entry.d_name) - 1);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
    return &dir->entry;
}

static inline int closedir(DIR *dir)
{
    if (!dir)
        return -1;
    _findclose(dir->handle);
    free(dir);
    return 0;
}

#endif