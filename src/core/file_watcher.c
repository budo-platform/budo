#include "core/file_watcher.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PATH_SEP '\\'
#else
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define WATCH_INTERVAL_SECONDS 0.25
#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME 1099511628211ULL

typedef struct
{
    uint64_t hash;
    uint64_t entries;
} FileSignature;

struct FileWatcher
{
    char root_dir[PATH_MAX];
    FileSignature signature;
    bool has_signature;
    double last_check_seconds;
    char error[256];
};

static void signature_mix_bytes(FileSignature *sig, const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++)
    {
        sig->hash ^= (uint64_t)bytes[i];
        sig->hash *= FNV_PRIME;
    }
}

static void signature_mix_u64(FileSignature *sig, uint64_t value)
{
    signature_mix_bytes(sig, &value, sizeof(value));
}

static void signature_mix_path(FileSignature *sig, const char *relative_path)
{
    signature_mix_bytes(sig, relative_path, strlen(relative_path));
    signature_mix_u64(sig, 0xff);
}

static void signature_add_entry(FileSignature *sig,
                                const char *relative_path,
                                uint64_t size,
                                uint64_t mtime_ns,
                                bool directory)
{
    sig->entries++;
    signature_mix_path(sig, relative_path);
    signature_mix_u64(sig, size);
    signature_mix_u64(sig, mtime_ns);
    signature_mix_u64(sig, directory ? 1 : 0);
}

static bool signatures_equal(FileSignature a, FileSignature b)
{
    return a.hash == b.hash && a.entries == b.entries;
}

static bool join_path(char *out, size_t out_size, const char *base, const char *name)
{
    int written;

    if (!base || base[0] == '\0')
        written = snprintf(out, out_size, "%s", name);
    else
        written = snprintf(out, out_size, "%s%c%s", base, PATH_SEP, name);
    return written > 0 && (size_t)written < out_size;
}

static bool join_relative_path(char *out, size_t out_size, const char *base, const char *name)
{
    int written;

    if (!base || base[0] == '\0')
        written = snprintf(out, out_size, "%s", name);
    else
        written = snprintf(out, out_size, "%s/%s", base, name);
    return written > 0 && (size_t)written < out_size;
}

#ifdef _WIN32
static uint64_t filetime_to_ns(const FILETIME *filetime)
{
    ULARGE_INTEGER value;
    value.LowPart = filetime->dwLowDateTime;
    value.HighPart = filetime->dwHighDateTime;
    return value.QuadPart * 100ULL;
}

static bool scan_directory(FileWatcher *watcher, const char *absolute_dir,
                           const char *relative_dir, FileSignature *sig)
{
    char search_path[PATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE handle;

    int written = snprintf(search_path, sizeof(search_path), "%s%c*", absolute_dir, PATH_SEP);
    if (written <= 0 || (size_t)written >= sizeof(search_path))
    {
        snprintf(watcher->error, sizeof(watcher->error), "watch path is too long");
        return false;
    }

    handle = FindFirstFileA(search_path, &data);
    if (handle == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return true;
        snprintf(watcher->error, sizeof(watcher->error), "cannot scan '%s'", absolute_dir);
        return false;
    }

    do
    {
        char child_abs[PATH_MAX];
        char child_rel[PATH_MAX];
        bool is_dir;
        uint64_t size;
        uint64_t mtime_ns;

        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        if (!join_path(child_abs, sizeof(child_abs), absolute_dir, data.cFileName) ||
            !join_relative_path(child_rel, sizeof(child_rel), relative_dir, data.cFileName))
        {
            snprintf(watcher->error, sizeof(watcher->error), "watch path is too long");
            FindClose(handle);
            return false;
        }

        is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        size = ((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow;
        mtime_ns = filetime_to_ns(&data.ftLastWriteTime);
        signature_add_entry(sig, child_rel, is_dir ? 0 : size, mtime_ns, is_dir);

        if (is_dir && !scan_directory(watcher, child_abs, child_rel, sig))
        {
            FindClose(handle);
            return false;
        }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
    return true;
}
#else
static uint64_t stat_mtime_ns(const struct stat *st)
{
#if defined(__APPLE__)
    return ((uint64_t)st->st_mtimespec.tv_sec * 1000000000ULL) + (uint64_t)st->st_mtimespec.tv_nsec;
#elif defined(__linux__)
    return ((uint64_t)st->st_mtim.tv_sec * 1000000000ULL) + (uint64_t)st->st_mtim.tv_nsec;
#else
    return (uint64_t)st->st_mtime * 1000000000ULL;
#endif
}

static bool scan_directory(FileWatcher *watcher, const char *absolute_dir,
                           const char *relative_dir, FileSignature *sig)
{
    DIR *dir = opendir(absolute_dir);
    struct dirent *entry;

    if (!dir)
    {
        snprintf(watcher->error, sizeof(watcher->error), "cannot scan '%s': %s",
                 absolute_dir, strerror(errno));
        return false;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char child_abs[PATH_MAX];
        char child_rel[PATH_MAX];
        struct stat st;
        bool is_dir;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!join_path(child_abs, sizeof(child_abs), absolute_dir, entry->d_name) ||
            !join_relative_path(child_rel, sizeof(child_rel), relative_dir, entry->d_name))
        {
            snprintf(watcher->error, sizeof(watcher->error), "watch path is too long");
            closedir(dir);
            return false;
        }
        if (lstat(child_abs, &st) != 0)
        {
            if (errno == ENOENT)
                continue;
            snprintf(watcher->error, sizeof(watcher->error), "cannot stat '%s': %s",
                     child_abs, strerror(errno));
            closedir(dir);
            return false;
        }
        if (S_ISLNK(st.st_mode))
            continue;
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
            continue;

        is_dir = S_ISDIR(st.st_mode);
        signature_add_entry(sig, child_rel, is_dir ? 0 : (uint64_t)st.st_size,
                            stat_mtime_ns(&st), is_dir);

        if (is_dir && !scan_directory(watcher, child_abs, child_rel, sig))
        {
            closedir(dir);
            return false;
        }
    }

    closedir(dir);
    return true;
}
#endif

static bool collect_signature(FileWatcher *watcher, FileSignature *sig)
{
    struct stat st;

    sig->hash = FNV_OFFSET;
    sig->entries = 0;
    watcher->error[0] = '\0';

    if (stat(watcher->root_dir, &st) != 0)
    {
        snprintf(watcher->error, sizeof(watcher->error), "cannot stat watch root '%s'",
                 watcher->root_dir);
        return false;
    }

    if (S_ISREG(st.st_mode))
    {
#ifdef _WIN32
        uint64_t mtime_ns = (uint64_t)st.st_mtime * 1000000000ULL;
#else
        uint64_t mtime_ns = stat_mtime_ns(&st);
#endif
        signature_add_entry(sig, ".", (uint64_t)st.st_size, mtime_ns, false);
        return true;
    }

    if (!S_ISDIR(st.st_mode))
    {
        snprintf(watcher->error, sizeof(watcher->error), "watch root is not a file or directory: '%s'",
                 watcher->root_dir);
        return false;
    }

    signature_add_entry(sig, ".", 0, 0, true);
    return scan_directory(watcher, watcher->root_dir, "", sig);
}

FileWatcher *file_watcher_create(const char *root_dir, double now_seconds)
{
    FileWatcher *watcher = (FileWatcher *)calloc(1, sizeof(FileWatcher));
    FileSignature sig;

    if (!watcher)
        return NULL;
    if (!root_dir || root_dir[0] == '\0' || strlen(root_dir) >= sizeof(watcher->root_dir))
    {
        free(watcher);
        return NULL;
    }

    snprintf(watcher->root_dir, sizeof(watcher->root_dir), "%s", root_dir);
    watcher->last_check_seconds = now_seconds;
    if (collect_signature(watcher, &sig))
    {
        watcher->signature = sig;
        watcher->has_signature = true;
    }
    return watcher;
}

void file_watcher_destroy(FileWatcher *watcher)
{
    free(watcher);
}

bool file_watcher_poll(FileWatcher *watcher, double now_seconds)
{
    FileSignature sig;

    if (!watcher)
        return false;
    if (now_seconds - watcher->last_check_seconds < WATCH_INTERVAL_SECONDS)
        return false;

    watcher->last_check_seconds = now_seconds;
    if (!collect_signature(watcher, &sig))
        return false;

    if (!watcher->has_signature)
    {
        watcher->signature = sig;
        watcher->has_signature = true;
        return false;
    }

    if (!signatures_equal(watcher->signature, sig))
    {
        watcher->signature = sig;
        return true;
    }
    return false;
}

const char *file_watcher_get_error(const FileWatcher *watcher)
{
    if (!watcher || watcher->error[0] == '\0')
        return NULL;
    return watcher->error;
}