#ifndef BUDO_FILE_WATCHER_H
#define BUDO_FILE_WATCHER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct FileWatcher FileWatcher;

    FileWatcher *file_watcher_create(const char *root_dir, double now_seconds);
    void file_watcher_destroy(FileWatcher *watcher);

    bool file_watcher_poll(FileWatcher *watcher, double now_seconds);
    const char *file_watcher_get_error(const FileWatcher *watcher);

#ifdef __cplusplus
}
#endif

#endif