#include "fs_util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define lstat stat
#define rmdir _rmdir
#define unlink _unlink
#else
#include <unistd.h>
#endif

#define FS_PATH_SIZE 4096

bool fs_make_temp_dir(const char *prefix, char *out, size_t out_size)
{
    const char *base = getenv("TMPDIR");
    size_t base_length;
    int written;

    if (!prefix || !out || out_size == 0)
    {
        errno = EINVAL;
        return false;
    }
    out[0] = '\0';
    if (!base || base[0] == '\0')
        base = "/tmp";
    base_length = strlen(base);
    while (base_length > 1 && base[base_length - 1] == '/')
        base_length--;

    written = snprintf(out, out_size, "%.*s/%s-XXXXXX", (int)base_length, base, prefix);
    if (written < 0 || (size_t)written >= out_size)
    {
        out[0] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }
#ifdef _WIN32
    if (!_mktemp(out) || _mkdir(out) != 0)
#else
    if (!mkdtemp(out))
#endif
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool fs_remove_tree(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    bool ok = true;

    if (!directory)
        return errno == ENOENT;
    while ((entry = readdir(directory)) != NULL)
    {
        char child[FS_PATH_SIZE];
        struct stat info;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
                (int)sizeof(child) ||
            lstat(child, &info) != 0)
            ok = false;
        else if (S_ISDIR(info.st_mode))
            ok = fs_remove_tree(child) && ok;
        else if (unlink(child) != 0)
            ok = false;
    }
    closedir(directory);
    return rmdir(path) == 0 && ok;
}