#ifndef FILE_WRAPPER_H
#define FILE_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FILE_MAX_PATH 4096

    typedef enum
    {
        FILE_ENTRY_FILE = 0,
        FILE_ENTRY_DIRECTORY = 1
    } FileEntryType;

    typedef struct
    {
        char name[256];
        FileEntryType type;
        int64_t size; 
    } FileEntry;

    typedef struct
    {
        int count;
        FileEntry *entries;
    } FileListResult;

    typedef struct FileContext FileContext;

    typedef enum
    {
        FILE_ROOT_ASSETS = 0,
        FILE_ROOT_FILES = 1
    } FileRoot;

    typedef struct
    {
#ifdef _WIN32
        void *handle;
#else
    int fd;
#endif
        uint64_t size;
        uint64_t identity_high;
        uint64_t identity_low;
    } FileNativeReference;

    FileContext *file_create(const char *root_dir);

    void file_destroy(FileContext *ctx);

    bool file_has_root(FileContext *ctx);

    bool file_path_is_valid(const char *rel_path, bool allow_empty);

    bool file_virtual_path_is_valid(const char *path, bool allow_root, bool writable);

    void file_set_root(FileContext *ctx, const char *root);

    FileListResult *file_list(FileContext *ctx, const char *rel_path);

    FileListResult *file_list_at(FileContext *ctx, FileRoot root, const char *rel_path);

    void file_list_free(FileListResult *result);

    bool file_is_file(FileContext *ctx, const char *rel_path);

    bool file_is_file_at(FileContext *ctx, FileRoot root, const char *rel_path);

    bool file_is_directory(FileContext *ctx, const char *rel_path);

    bool file_is_directory_at(FileContext *ctx, FileRoot root, const char *rel_path);

    int64_t file_size(FileContext *ctx, const char *rel_path);

    int64_t file_size_at(FileContext *ctx, FileRoot root, const char *rel_path);

    char *file_read_text(FileContext *ctx, const char *rel_path, size_t *out_len);

    char *file_read_text_at(FileContext *ctx, FileRoot root, const char *rel_path,
                            size_t *out_len);

    uint8_t *file_read_binary(FileContext *ctx, const char *rel_path, size_t *out_len);

    uint8_t *file_read_binary_at(FileContext *ctx, FileRoot root, const char *rel_path,
                                 size_t *out_len);

    bool file_native_open(FileContext *ctx, const char *path, FileNativeReference *out_ref);

    void file_native_close(FileNativeReference *ref);

    void file_set_write_root(FileContext *ctx, const char *write_root);

    bool file_has_write_root(FileContext *ctx);

    bool file_write_text(FileContext *ctx, const char *rel_path, const char *text, size_t len);

    bool file_write_binary(FileContext *ctx, const char *rel_path, const uint8_t *data, size_t len);

    bool file_append_binary(FileContext *ctx, const char *rel_path,
                            const uint8_t *data, size_t len);

    const char *file_get_last_write_path(FileContext *ctx);

    const char *file_get_error(FileContext *ctx);

#ifdef __cplusplus
}
#endif

#endif