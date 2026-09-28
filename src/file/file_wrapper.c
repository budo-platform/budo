#include "file_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

struct FileContext
{
    char root_dir[FILE_MAX_PATH];
    char write_dir[FILE_MAX_PATH];
    char last_write_path[FILE_MAX_PATH];
    char error[512];
#ifdef _WIN32
    HANDLE root_handle;
    HANDLE write_handle;
#else
    int root_fd;
    int write_fd;
#endif
};

bool file_path_is_valid(const char *rel_path, bool allow_empty)
{
    if (!rel_path)
        return false;
    if (!rel_path[0])
        return allow_empty;
    if (rel_path[0] == '/')
        return false;
#ifdef _WIN32
    if (rel_path[0] == '\\')
        return false;
#endif
    const char *p = rel_path;
    while (*p)
    {
        while (*p == '/')
            p++;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t length = (size_t)(p - start);
        if (length == 2 && start[0] == '.' && start[1] == '.')
            return false;
        if (length == 0 || length >= 256)
            return false;
#ifdef _WIN32
        if (memchr(start, '\\', length) || memchr(start, ':', length))
            return false;
#endif
    }
    return true;
}

bool file_virtual_path_is_valid(const char *path, bool allow_root, bool writable)
{
    if (!writable && (strcmp(path, "assets/") == 0 || strcmp(path, "files/") == 0))
        return true;
    if (!file_path_is_valid(path, allow_root))
        return false;
    if (!path[0])
        return allow_root && !writable;
    if (strncmp(path, "files/", 6) == 0)
        return path[6] != '\0' || !writable;
    if (!writable && strcmp(path, "files") == 0)
        return true;
    if (!writable && strncmp(path, "assets/", 7) == 0)
        return true;
    return !writable && strcmp(path, "assets") == 0;
}

static bool validate_relative_path(FileContext *ctx, const char *rel_path,
                                   bool allow_empty)
{
    if (file_path_is_valid(rel_path, allow_empty))
        return true;
    if (ctx)
        snprintf(ctx->error, sizeof(ctx->error), "Invalid relative file path");
    return false;
}

static void clear_error(FileContext *ctx)
{
    if (ctx)
        ctx->error[0] = '\0';
}

static const char *write_base_dir(FileContext *ctx)
{
    if (!ctx || !ctx->write_dir[0])
        return NULL;
    return ctx->write_dir;
}

#ifdef _WIN32
static HANDLE root_handle_for(FileContext *ctx, FileRoot root)
{
    return root == FILE_ROOT_FILES ? ctx->write_handle : ctx->root_handle;
}
#else
static int root_fd_for(FileContext *ctx, FileRoot root)
{
    return root == FILE_ROOT_FILES ? ctx->write_fd : ctx->root_fd;
}
#endif

static bool resolve_virtual_path(FileContext *ctx, const char *path,
                                 FileRoot *root, const char **relative)
{
    if (strncmp(path, "assets", 6) == 0 && (path[6] == '/' || path[6] == '\0'))
    {
        *root = FILE_ROOT_ASSETS;
        *relative = path[6] == '/' ? path + 7 : path + 6;
        return true;
    }
    if (strncmp(path, "files", 5) == 0 && (path[5] == '/' || path[5] == '\0'))
    {
        *root = FILE_ROOT_FILES;
        *relative = path[5] == '/' ? path + 6 : path + 5;
        return true;
    }
    snprintf(ctx->error, sizeof(ctx->error),
             "Path must start with assets/ or files/");
    return false;
}

static FileListResult *virtual_root_listing(FileContext *ctx)
{
    FileListResult *result = malloc(sizeof(*result));
    FileEntry *entries = calloc(2, sizeof(*entries));
    if (!result || !entries)
    {
        free(result);
        free(entries);
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }
    snprintf(entries[0].name, sizeof(entries[0].name), "assets");
    entries[0].type = FILE_ENTRY_DIRECTORY;
    entries[0].size = -1;
    snprintf(entries[1].name, sizeof(entries[1].name), "files");
    entries[1].type = FILE_ENTRY_DIRECTORY;
    entries[1].size = -1;
    result->count = 2;
    result->entries = entries;
    clear_error(ctx);
    return result;
}

static bool next_component(const char **cursor, char component[256], bool *last)
{
    const char *p = *cursor;
    while (*p == '/')
        p++;
    if (!*p)
        return false;
    const char *start = p;
    while (*p && *p != '/')
        p++;
    size_t length = (size_t)(p - start);
    memcpy(component, start, length);
    component[length] = '\0';
    while (*p == '/')
        p++;
    *last = !*p;
    *cursor = p;
    return true;
}

#ifdef _WIN32
static void set_windows_error(FileContext *ctx, const char *operation)
{
    snprintf(ctx->error, sizeof(ctx->error), "%s (Windows error %lu)",
             operation, (unsigned long)GetLastError());
}

static bool utf8_to_wide(const char *text, wchar_t *wide, size_t capacity)
{
    if (!text || capacity > INT_MAX)
        return false;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                    wide, (int)capacity);
    return count > 0;
}

static bool handle_is_safe_type(FileContext *ctx, HANDLE handle, bool require_directory,
                                bool require_file)
{
    FILE_ATTRIBUTE_TAG_INFO tag;
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)))
    {
        set_windows_error(ctx, "Cannot inspect sandbox handle");
        return false;
    }
    if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Reparse points are not allowed in sandbox paths");
        return false;
    }
    bool directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if ((require_directory && !directory) || (require_file && directory))
    {
        snprintf(ctx->error, sizeof(ctx->error),
                 require_directory ? "Sandbox path is not a directory" : "Sandbox path is not a file");
        return false;
    }
    return true;
}

static HANDLE duplicate_handle(FileContext *ctx, HANDLE source)
{
    HANDLE duplicate = INVALID_HANDLE_VALUE;
    if (source == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        snprintf(ctx->error, sizeof(ctx->error), "File root not configured");
        return INVALID_HANDLE_VALUE;
    }
    return duplicate;
}

static HANDLE open_root_directory(FileContext *ctx, const char *path)
{
    wchar_t wide_path[FILE_MAX_PATH];
    if (!utf8_to_wide(path, wide_path, FILE_MAX_PATH))
    {
        snprintf(ctx->error, sizeof(ctx->error), "File root is not valid UTF-8 or is too long");
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle = CreateFileW(wide_path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        set_windows_error(ctx, "Cannot open file root");
        return handle;
    }
    if (!handle_is_safe_type(ctx, handle, true, false))
    {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

static HANDLE open_child(FileContext *ctx, HANDLE directory, const char *component,
                         DWORD access, DWORD creation, bool require_directory,
                         bool require_file)
{
    wchar_t wide_component[256];
    if (!utf8_to_wide(component, wide_component, 256))
    {
        snprintf(ctx->error, sizeof(ctx->error), "Invalid UTF-8 sandbox path component");
        return INVALID_HANDLE_VALUE;
    }

    typedef NTSTATUS(NTAPI * NtCreateFileFunction)(
        PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER,
        ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    static NtCreateFileFunction nt_create_file;
    static bool resolved;
    if (!resolved)
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        nt_create_file = ntdll ? (NtCreateFileFunction)(void *)GetProcAddress(ntdll, "NtCreateFile")
                               : NULL;
        resolved = true;
    }
    if (!nt_create_file)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Secure relative file opens are unavailable");
        return INVALID_HANDLE_VALUE;
    }

    UNICODE_STRING name;
    size_t component_length = wcslen(wide_component);
    name.Buffer = wide_component;
    name.Length = (USHORT)(component_length * sizeof(wchar_t));
    name.MaximumLength = name.Length + sizeof(wchar_t);
    OBJECT_ATTRIBUTES attributes;
#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif
#ifndef FILE_OPEN_REPARSE_POINT
#define FILE_OPEN_REPARSE_POINT 0x00200000
#endif
#ifndef FILE_SYNCHRONOUS_IO_NONALERT
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#endif
    InitializeObjectAttributes(&attributes, &name,
                               OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE,
                               directory, NULL);
    IO_STATUS_BLOCK status_block;
    HANDLE handle = INVALID_HANDLE_VALUE;
    ULONG disposition = creation == OPEN_ALWAYS  ? FILE_OPEN_IF
                        : creation == CREATE_NEW ? FILE_CREATE
                                                 : FILE_OPEN;
    ULONG options = FILE_OPEN_REPARSE_POINT;
    if (require_directory)
        options |= FILE_DIRECTORY_FILE;
    if (require_file)
        options |= FILE_NON_DIRECTORY_FILE;
    NTSTATUS status = nt_create_file(&handle, access | SYNCHRONIZE, &attributes,
                                     &status_block, NULL, FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     disposition, options | FILE_SYNCHRONOUS_IO_NONALERT,
                                     NULL, 0);
    if (status < 0)
    {
        typedef ULONG(WINAPI * RtlNtStatusToDosErrorFunction)(NTSTATUS);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        RtlNtStatusToDosErrorFunction to_dos_error =
            ntdll ? (RtlNtStatusToDosErrorFunction)(void *)GetProcAddress(
                        ntdll, "RtlNtStatusToDosError")
                  : NULL;
        SetLastError(to_dos_error ? to_dos_error(status) : ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    if (!handle_is_safe_type(ctx, handle, require_directory, require_file))
    {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

static HANDLE open_relative(FileContext *ctx, HANDLE root, const char *rel_path,
                            DWORD final_access, bool final_directory, bool final_file)
{
    if (!validate_relative_path(ctx, rel_path, true))
        return INVALID_HANDLE_VALUE;
    HANDLE current = duplicate_handle(ctx, root);
    if (current == INVALID_HANDLE_VALUE)
        return current;

    const char *cursor = rel_path;
    char component[256];
    bool last;
    while (next_component(&cursor, component, &last))
    {
        HANDLE next = open_child(ctx, current, component,
                                 last ? final_access : (FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES),
                                 OPEN_EXISTING,
                                 last ? final_directory : true,
                                 last ? final_file : false);
        if (next == INVALID_HANDLE_VALUE)
        {
            if (!ctx->error[0])
                set_windows_error(ctx, "Cannot open sandbox path");
            CloseHandle(current);
            return INVALID_HANDLE_VALUE;
        }
        CloseHandle(current);
        current = next;
    }
    return current;
}

static HANDLE open_write_file(FileContext *ctx, const char *rel_path, bool append)
{
    HANDLE root = ctx->write_dir[0] ? ctx->write_handle : ctx->root_handle;
    if (root == INVALID_HANDLE_VALUE || !validate_relative_path(ctx, rel_path, false))
    {
        if (!ctx->error[0])
            snprintf(ctx->error, sizeof(ctx->error), "A file path is required");
        return INVALID_HANDLE_VALUE;
    }

    HANDLE current = duplicate_handle(ctx, root);
    if (current == INVALID_HANDLE_VALUE)
        return current;
    const char *cursor = rel_path;
    char component[256];
    bool last;
    while (next_component(&cursor, component, &last))
    {
        if (last)
        {
            HANDLE file = open_child(ctx, current, component,
                                     GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                                     OPEN_ALWAYS, false, true);
            if (file == INVALID_HANDLE_VALUE && !ctx->error[0])
                set_windows_error(ctx, "Cannot open file for writing");
            CloseHandle(current);
            LARGE_INTEGER beginning;
            beginning.QuadPart = 0;
            if (file != INVALID_HANDLE_VALUE &&
                !SetFilePointerEx(file, beginning, NULL,
                                  append ? FILE_END : FILE_BEGIN))
            {
                set_windows_error(ctx, "Cannot seek writable file");
                CloseHandle(file);
                return INVALID_HANDLE_VALUE;
            }
            if (file != INVALID_HANDLE_VALUE && !append && !SetEndOfFile(file))
            {
                set_windows_error(ctx, "Cannot truncate writable file");
                CloseHandle(file);
                return INVALID_HANDLE_VALUE;
            }
            return file;
        }

        HANDLE next = open_child(ctx, current, component,
                                 FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                 OPEN_EXISTING, true, false);
        if (next == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            next = open_child(ctx, current, component,
                              FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                              CREATE_NEW, true, false);
            if (next == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ALREADY_EXISTS)
                next = open_child(ctx, current, component,
                                  FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                  OPEN_EXISTING, true, false);
        }
        if (next == INVALID_HANDLE_VALUE)
        {
            if (!ctx->error[0])
                set_windows_error(ctx, "Cannot open sandbox directory");
            CloseHandle(current);
            return INVALID_HANDLE_VALUE;
        }
        CloseHandle(current);
        current = next;
    }
    CloseHandle(current);
    return INVALID_HANDLE_VALUE;
}
#else
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#error "A no-follow open flag is required for the guest file sandbox"
#endif

static int open_root_directory(FileContext *ctx, const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        snprintf(ctx->error, sizeof(ctx->error), "Cannot open file root: %s", strerror(errno));
    return fd;
}

static int open_relative(FileContext *ctx, int root_fd, const char *rel_path,
                         int final_flags)
{
    if (root_fd < 0)
    {
        snprintf(ctx->error, sizeof(ctx->error), "File root not configured");
        return -1;
    }
    if (!validate_relative_path(ctx, rel_path, true))
        return -1;

    int current = dup(root_fd);
    if (current < 0)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Cannot access file root: %s", strerror(errno));
        return -1;
    }
    const char *cursor = rel_path;
    char component[256];
    bool last;
    while (next_component(&cursor, component, &last))
    {
        int flags = last ? final_flags : (O_RDONLY | O_DIRECTORY);
        int next = openat(current, component, flags | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0)
        {
            snprintf(ctx->error, sizeof(ctx->error), "Cannot open sandbox path: %s", strerror(errno));
            close(current);
            return -1;
        }
        close(current);
        current = next;
    }
    return current;
}

static int open_write_file(FileContext *ctx, const char *rel_path, bool append)
{
    int root_fd = ctx->write_dir[0] ? ctx->write_fd : ctx->root_fd;
    if (root_fd < 0 || !validate_relative_path(ctx, rel_path, false))
    {
        if (ctx->error[0] == '\0')
            snprintf(ctx->error, sizeof(ctx->error), "A file path is required");
        return -1;
    }

    int current = dup(root_fd);
    if (current < 0)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Cannot access writable file root: %s", strerror(errno));
        return -1;
    }
    const char *cursor = rel_path;
    char component[256];
    bool last;
    while (next_component(&cursor, component, &last))
    {
        if (last)
        {
            int fd = openat(current, component,
                            O_WRONLY | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC |
                                (append ? O_APPEND : 0),
                            0644);
            if (fd < 0)
                snprintf(ctx->error, sizeof(ctx->error), "Cannot open file for writing: %s", strerror(errno));
            close(current);
            if (fd >= 0)
            {
                struct stat st;
                if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
                    (!append && ftruncate(fd, 0) != 0))
                {
                    snprintf(ctx->error, sizeof(ctx->error), "Write target is not a regular file");
                    close(fd);
                    return -1;
                }
            }
            return fd;
        }
        int next = openat(current, component,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT)
        {
            if (mkdirat(current, component, 0755) != 0 && errno != EEXIST)
            {
                snprintf(ctx->error, sizeof(ctx->error), "Cannot create directory: %s", strerror(errno));
                close(current);
                return -1;
            }
            next = openat(current, component,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0)
        {
            snprintf(ctx->error, sizeof(ctx->error), "Cannot open sandbox directory: %s", strerror(errno));
            close(current);
            return -1;
        }
        close(current);
        current = next;
    }
    close(current);
    return -1;
}
#endif

#ifdef __EMSCRIPTEN__

EM_JS(void, budo_web_download_file, (const char *namePtr, const char *dataPtr, int len, const char *mimePtr), {
    var filename = UTF8ToString(namePtr);
    var mime = UTF8ToString(mimePtr);
    var bytes = new Uint8Array(len);
    bytes.set(HEAPU8.subarray(dataPtr, dataPtr + len));
    var blob = new Blob([bytes],
                        { type : mime });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
});

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash && slash[1])
        return slash + 1;
    return path;
}
#endif

FileContext *file_create(const char *root_dir)
{
    FileContext *ctx = (FileContext *)calloc(1, sizeof(FileContext));
    if (!ctx)
        return NULL;

#ifdef _WIN32
    ctx->root_handle = INVALID_HANDLE_VALUE;
    ctx->write_handle = INVALID_HANDLE_VALUE;
#else
    ctx->root_fd = -1;
    ctx->write_fd = -1;
#endif

    if (root_dir && root_dir[0])
    {
        snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", root_dir);
#ifdef _WIN32
        ctx->root_handle = open_root_directory(ctx, root_dir);
#else
        ctx->root_fd = open_root_directory(ctx, root_dir);
#endif
    }

    return ctx;
}

void file_destroy(FileContext *ctx)
{
    if (ctx)
    {
#ifdef _WIN32
        if (ctx->root_handle != INVALID_HANDLE_VALUE)
            CloseHandle(ctx->root_handle);
        if (ctx->write_handle != INVALID_HANDLE_VALUE)
            CloseHandle(ctx->write_handle);
#else
        if (ctx->root_fd >= 0)
            close(ctx->root_fd);
        if (ctx->write_fd >= 0)
            close(ctx->write_fd);
#endif
        free(ctx);
    }
}

bool file_has_root(FileContext *ctx)
{
#ifdef _WIN32
    return ctx && ctx->root_handle != INVALID_HANDLE_VALUE;
#else
    return ctx && ctx->root_fd >= 0;
#endif
}

void file_set_root(FileContext *ctx, const char *root)
{
    if (!ctx)
        return;
#ifdef _WIN32
    HANDLE replacement = INVALID_HANDLE_VALUE;
    if (root && root[0])
    {
        replacement = open_root_directory(ctx, root);
        if (replacement == INVALID_HANDLE_VALUE)
            return;
    }
    if (ctx->root_handle != INVALID_HANDLE_VALUE)
        CloseHandle(ctx->root_handle);
    ctx->root_handle = replacement;
    if (root && root[0])
        snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", root);
    else
        ctx->root_dir[0] = '\0';
    clear_error(ctx);
#else
    int replacement = -1;
    if (root && root[0])
    {
        replacement = open_root_directory(ctx, root);
        if (replacement < 0)
            return;
    }
    if (ctx->root_fd >= 0)
        close(ctx->root_fd);
    ctx->root_fd = replacement;
    if (root && root[0])
        snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", root);
    else
        ctx->root_dir[0] = '\0';
    clear_error(ctx);
#endif
}

static int entry_compare(const void *a, const void *b)
{
    const FileEntry *ea = (const FileEntry *)a;
    const FileEntry *eb = (const FileEntry *)b;

    if (ea->type != eb->type)
        return ea->type == FILE_ENTRY_DIRECTORY ? -1 : 1;

    return strcmp(ea->name, eb->name);
}

FileListResult *file_list_at(FileContext *ctx, FileRoot root, const char *rel_path)
{
    if (!ctx)
        return NULL;

#ifdef _WIN32
    HANDLE directory = open_relative(ctx, root_handle_for(ctx, root), rel_path,
                                     FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                     true, false);
    if (directory == INVALID_HANDLE_VALUE)
        return NULL;
    int capacity = 64;
    int count = 0;
    FileEntry *entries = malloc((size_t)capacity * sizeof(FileEntry));
    size_t query_size = 64 * 1024;
    void *query = malloc(query_size);
    if (!entries || !query)
    {
        free(entries);
        free(query);
        CloseHandle(directory);
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }
    for (;;)
    {
        if (!GetFileInformationByHandleEx(directory, FileIdBothDirectoryInfo,
                                          query, (DWORD)query_size))
        {
            DWORD query_error = GetLastError();
            if (query_error == ERROR_NO_MORE_FILES)
                break;
            free(entries);
            free(query);
            CloseHandle(directory);
            SetLastError(query_error);
            set_windows_error(ctx, "Cannot list sandbox directory");
            return NULL;
        }
        FILE_ID_BOTH_DIR_INFO *data = query;
        for (;;)
        {
            size_t name_length = data->FileNameLength / sizeof(wchar_t);
            bool dot = name_length == 1 && data->FileName[0] == L'.';
            bool dot_dot = name_length == 2 && data->FileName[0] == L'.' &&
                           data->FileName[1] == L'.';
            if (!dot && !dot_dot && name_length > 0 && data->FileName[0] != L'.')
            {
                if (count == capacity)
                {
                    capacity *= 2;
                    FileEntry *replacement = realloc(
                        entries, (size_t)capacity * sizeof(FileEntry));
                    if (!replacement)
                    {
                        free(entries);
                        free(query);
                        CloseHandle(directory);
                        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
                        return NULL;
                    }
                    entries = replacement;
                }
                int converted = WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, data->FileName, (int)name_length,
                    entries[count].name, (int)sizeof(entries[count].name) - 1,
                    NULL, NULL);
                if (converted > 0)
                {
                    entries[count].name[converted] = '\0';
                    bool is_reparse =
                        (data->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    bool is_directory =
                        (data->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    entries[count].type = is_directory && !is_reparse
                                              ? FILE_ENTRY_DIRECTORY
                                              : FILE_ENTRY_FILE;
                    entries[count].size = entries[count].type == FILE_ENTRY_DIRECTORY
                                              ? -1
                                              : data->EndOfFile.QuadPart;
                    count++;
                }
            }
            if (data->NextEntryOffset == 0)
                break;
            data = (FILE_ID_BOTH_DIR_INFO *)((unsigned char *)data +
                                             data->NextEntryOffset);
        }
    }
    free(query);
    CloseHandle(directory);
    if (count > 0)
        qsort(entries, (size_t)count, sizeof(FileEntry), entry_compare);
    FileListResult *result = malloc(sizeof(FileListResult));
    if (!result)
    {
        free(entries);
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }
    result->count = count;
    result->entries = entries;
    clear_error(ctx);
    return result;
#else
    int directory_fd = open_relative(ctx, root_fd_for(ctx, root), rel_path,
                                     O_RDONLY | O_DIRECTORY);
    if (directory_fd < 0)
        return NULL;
    DIR *dir = fdopendir(directory_fd);
    if (!dir)
    {
        close(directory_fd);
        snprintf(ctx->error, sizeof(ctx->error), "Cannot open directory: %s", strerror(errno));
        return NULL;
    }

    int capacity = 64;
    int count = 0;
    FileEntry *entries = (FileEntry *)malloc(capacity * sizeof(FileEntry));
    if (!entries)
    {
        closedir(dir);
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL)
    {
        
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        if (de->d_name[0] == '.')
            continue;

        if (count >= capacity)
        {
            capacity *= 2;
            FileEntry *new_entries = (FileEntry *)realloc(entries, capacity * sizeof(FileEntry));
            if (!new_entries)
            {
                free(entries);
                closedir(dir);
                snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
                return NULL;
            }
            entries = new_entries;
        }

        snprintf(entries[count].name, sizeof(entries[count].name), "%s", de->d_name);

        struct stat st;
        if (fstatat(dirfd(dir), de->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                entries[count].type = FILE_ENTRY_DIRECTORY;
                entries[count].size = -1;
            }
            else
            {
                entries[count].type = FILE_ENTRY_FILE;
                entries[count].size = st.st_size;
            }
        }
        else
        {
            entries[count].type = FILE_ENTRY_FILE;
            entries[count].size = -1;
        }

        count++;
    }

    closedir(dir);

    if (count > 0)
        qsort(entries, count, sizeof(FileEntry), entry_compare);

    FileListResult *result = (FileListResult *)malloc(sizeof(FileListResult));
    if (!result)
    {
        free(entries);
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }

    result->count = count;
    result->entries = entries;
    clear_error(ctx);
    return result;
#endif
}

FileListResult *file_list(FileContext *ctx, const char *rel_path)
{
    if (!ctx || !rel_path)
        return NULL;
    if (!rel_path[0])
        return virtual_root_listing(ctx);
    FileRoot root;
    const char *relative;
    if (!resolve_virtual_path(ctx, rel_path, &root, &relative))
        return NULL;
    return file_list_at(ctx, root, relative);
}

void file_list_free(FileListResult *result)
{
    if (result)
    {
        free(result->entries);
        free(result);
    }
}

bool file_is_file_at(FileContext *ctx, FileRoot root, const char *rel_path)
{
    if (!ctx)
        return false;

#ifdef _WIN32
    HANDLE handle = open_relative(ctx, root_handle_for(ctx, root), rel_path,
                                  FILE_READ_ATTRIBUTES, false, true);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(handle);
    clear_error(ctx);
    return true;
#else
    int fd = open_relative(ctx, root_fd_for(ctx, root), rel_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;
    struct stat st;
    bool stat_ok = fstat(fd, &st) == 0;
    bool result = stat_ok && S_ISREG(st.st_mode);
    close(fd);
    if (stat_ok)
        clear_error(ctx);
    else
        snprintf(ctx->error, sizeof(ctx->error), "Cannot inspect file: %s", strerror(errno));
    return result;
#endif
}

bool file_is_file(FileContext *ctx, const char *rel_path)
{
    if (!ctx || !rel_path)
        return false;
    FileRoot root;
    const char *relative;
    if (!resolve_virtual_path(ctx, rel_path, &root, &relative) || !relative[0])
        return false;
    return file_is_file_at(ctx, root, relative);
}

bool file_is_directory_at(FileContext *ctx, FileRoot root, const char *rel_path)
{
    if (!ctx)
        return false;

#ifdef _WIN32
    HANDLE handle = open_relative(ctx, root_handle_for(ctx, root), rel_path,
                                  FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                  true, false);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(handle);
    clear_error(ctx);
    return true;
#else
    int fd = open_relative(ctx, root_fd_for(ctx, root), rel_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return false;
    struct stat st;
    bool stat_ok = fstat(fd, &st) == 0;
    bool result = stat_ok && S_ISDIR(st.st_mode);
    close(fd);
    if (stat_ok)
        clear_error(ctx);
    else
        snprintf(ctx->error, sizeof(ctx->error), "Cannot inspect directory: %s", strerror(errno));
    return result;
#endif
}

bool file_is_directory(FileContext *ctx, const char *rel_path)
{
    if (!ctx || !rel_path)
        return false;
    if (!rel_path[0])
    {
        clear_error(ctx);
        return true;
    }
    FileRoot root;
    const char *relative;
    if (!resolve_virtual_path(ctx, rel_path, &root, &relative))
        return false;
    if (!relative[0])
    {
        clear_error(ctx);
        return true;
    }
    return file_is_directory_at(ctx, root, relative);
}

int64_t file_size_at(FileContext *ctx, FileRoot root, const char *rel_path)
{
    if (!ctx)
        return -1;

#ifdef _WIN32
    HANDLE handle = open_relative(ctx, root_handle_for(ctx, root), rel_path,
                                  FILE_READ_ATTRIBUTES, false, true);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0)
    {
        set_windows_error(ctx, "Cannot determine file size");
        CloseHandle(handle);
        return -1;
    }
    CloseHandle(handle);
    clear_error(ctx);
    return size.QuadPart;
#else
    int fd = open_relative(ctx, root_fd_for(ctx, root), rel_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        close(fd);
        snprintf(ctx->error, sizeof(ctx->error), "File not found: %s", rel_path);
        return -1;
    }

    close(fd);
    clear_error(ctx);
    return st.st_size;
#endif
}

int64_t file_size(FileContext *ctx, const char *rel_path)
{
    if (!ctx || !rel_path)
        return -1;
    FileRoot root;
    const char *relative;
    if (!resolve_virtual_path(ctx, rel_path, &root, &relative) || !relative[0])
        return -1;
    return file_size_at(ctx, root, relative);
}

#ifdef _WIN32
static uint8_t *read_open_file(FileContext *ctx, HANDLE handle, size_t extra,
                               size_t *out_len)
{
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(handle, &file_size) || file_size.QuadPart < 0 ||
        (uint64_t)file_size.QuadPart > SIZE_MAX - extra)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Cannot determine file size");
        return NULL;
    }
    size_t size = (size_t)file_size.QuadPart;
    uint8_t *buffer = malloc(size + extra + (size + extra == 0));
    if (!buffer)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }
    size_t total = 0;
    while (total < size)
    {
        DWORD chunk = size - total > MAXDWORD ? MAXDWORD : (DWORD)(size - total);
        DWORD received = 0;
        if (!ReadFile(handle, buffer + total, chunk, &received, NULL) || received == 0)
        {
            set_windows_error(ctx, "Failed to read file");
            free(buffer);
            return NULL;
        }
        total += received;
    }
    if (extra)
        buffer[total] = '\0';
    *out_len = total;
    clear_error(ctx);
    return buffer;
}
#else
static uint8_t *read_open_file(FileContext *ctx, int fd, size_t extra, size_t *out_len)
{
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uintmax_t)st.st_size > SIZE_MAX - extra)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Cannot determine file size");
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    uint8_t *buffer = malloc(size + extra + (size + extra == 0));
    if (!buffer)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory");
        return NULL;
    }
    size_t total = 0;
    while (total < size)
    {
        ssize_t count = read(fd, buffer + total, size - total);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            if (count == 0)
                snprintf(ctx->error, sizeof(ctx->error), "Unexpected end of file");
            else
                snprintf(ctx->error, sizeof(ctx->error), "Failed to read file: %s", strerror(errno));
            free(buffer);
            return NULL;
        }
        total += (size_t)count;
    }
    if (extra)
        buffer[total] = '\0';
    *out_len = total;
    clear_error(ctx);
    return buffer;
}
#endif

char *file_read_text_at(FileContext *ctx, FileRoot root, const char *rel_path,
                        size_t *out_len)
{
    if (!ctx)
        return NULL;

#ifdef _WIN32
    HANDLE handle = open_relative(ctx, root_handle_for(ctx, root), rel_path,
                                  GENERIC_READ | FILE_READ_ATTRIBUTES,
                                  false, true);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    size_t ignored;
    char *buffer = (char *)read_open_file(ctx, handle, 1, out_len ? out_len : &ignored);
    CloseHandle(handle);
    return buffer;
#else
    int fd = open_relative(ctx, root_fd_for(ctx, root), rel_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return NULL;
    size_t ignored;
    char *buffer = (char *)read_open_file(ctx, fd, 1, out_len ? out_len : &ignored);
    close(fd);
    return buffer;
#endif
}

char *file_read_text(FileContext *ctx, const char *rel_path, size_t *out_len)
{
    if (!ctx || !rel_path)
        return NULL;
    FileRoot root;
    const char *relative;
    if (!resolve_virtual_path(ctx, rel_path, &root, &relative) || !relative[0])
        return NULL;
    return file_read_text_at(ctx, root, relative, out_len);
}

uint8_t *file_read_binary_at(FileContext *ctx, FileRoot root, const char *rel_path,
                             size_t *out_len)
{
    if (!ctx || !out_len)
        return NULL;

#ifdef _WIN32
    HANDLE handle = open_relative(ctx, root_handle_for(ctx, root), rel_path,
                                  GENERIC_READ | FILE_READ_ATTRIBUTES,
                                  false, true);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    uint8_t *buffer = read_open_file(ctx, handle, 0, out_len);
    CloseHandle(handle);
    return buffer;
#else
    int fd = open_relative(ctx, root_fd_for(ctx, root), rel_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return NULL;
    uint8_t *buffer = read_open_file(ctx, fd, 0, out_len);
    close(fd);
    return buffer;
#endif
}

uint8_t *file_read_binary(FileContext *ctx, const char *rel_path, size_t *out_len)
{
    if (!ctx || !rel_path || !out_len)
        return NULL;
    FileRoot root;
    const char *relative;
    if (!resolve_virtual_path(ctx, rel_path, &root, &relative) || !relative[0])
        return NULL;
    return file_read_binary_at(ctx, root, relative, out_len);
}

bool file_native_open(FileContext *ctx, const char *path, FileNativeReference *out_ref)
{
    FileRoot root;
    const char *relative;
    if (!ctx || !path || !out_ref)
        return false;
    memset(out_ref, 0, sizeof(*out_ref));
#ifndef _WIN32
    out_ref->fd = -1;
#endif
    clear_error(ctx);
    if (!file_virtual_path_is_valid(path, false, false) ||
        !resolve_virtual_path(ctx, path, &root, &relative) || !relative[0])
    {
        if (!ctx->error[0])
            snprintf(ctx->error, sizeof(ctx->error), "A mounted file path is required");
        return false;
    }
#ifdef _WIN32
    HANDLE handle = open_relative(ctx, root_handle_for(ctx, root), relative,
                                  GENERIC_READ | FILE_READ_ATTRIBUTES, false, true);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info))
    {
        set_windows_error(ctx, "Cannot inspect sandbox file");
        CloseHandle(handle);
        return false;
    }
    out_ref->handle = handle;
    out_ref->size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    out_ref->identity_high = info.dwVolumeSerialNumber;
    out_ref->identity_low = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
#else
    int fd = open_relative(ctx, root_fd_for(ctx, root), relative, O_RDONLY);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Sandbox path is not a regular file");
        close(fd);
        return false;
    }
    out_ref->fd = fd;
    out_ref->size = (uint64_t)st.st_size;
    out_ref->identity_high = (uint64_t)st.st_dev;
    out_ref->identity_low = (uint64_t)st.st_ino;
#endif
    return true;
}

void file_native_close(FileNativeReference *ref)
{
    if (!ref)
        return;
#ifdef _WIN32
    if (ref->handle && ref->handle != INVALID_HANDLE_VALUE)
        CloseHandle((HANDLE)ref->handle);
    ref->handle = NULL;
#else
    if (ref->fd >= 0)
        close(ref->fd);
    ref->fd = -1;
#endif
    ref->size = 0;
    ref->identity_high = 0;
    ref->identity_low = 0;
}

void file_set_write_root(FileContext *ctx, const char *write_root)
{
    if (!ctx)
        return;
#ifdef _WIN32
    if (ctx->write_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(ctx->write_handle);
        ctx->write_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (ctx->write_fd >= 0)
    {
        close(ctx->write_fd);
        ctx->write_fd = -1;
    }
#endif
    if (write_root && write_root[0])
    {
        snprintf(ctx->write_dir, sizeof(ctx->write_dir), "%s", write_root);
#ifdef _WIN32
        ctx->write_handle = open_root_directory(ctx, write_root);
#else
        ctx->write_fd = open_root_directory(ctx, write_root);
#endif
    }
    else
        ctx->write_dir[0] = '\0';
#ifdef _WIN32
    if ((!write_root || !write_root[0]) || ctx->write_handle != INVALID_HANDLE_VALUE)
        clear_error(ctx);
#else
    if ((!write_root || !write_root[0]) || ctx->write_fd >= 0)
        clear_error(ctx);
#endif
}

bool file_has_write_root(FileContext *ctx)
{
#ifdef _WIN32
    return ctx && ctx->write_dir[0] && ctx->write_handle != INVALID_HANDLE_VALUE;
#else
    return ctx && ctx->write_dir[0] && ctx->write_fd >= 0;
#endif
}

static bool file_write_bytes(FileContext *ctx, const char *rel_path,
                             const void *data, size_t len, bool append)
{
    if (!ctx)
        return false;
#ifdef _WIN32
    if (len > 0 && !data)
        return false;
    HANDLE handle = open_write_file(ctx, rel_path, append);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const uint8_t *bytes = data;
    size_t written = 0;
    while (written < len)
    {
        DWORD chunk = len - written > MAXDWORD ? MAXDWORD : (DWORD)(len - written);
        DWORD count = 0;
        if (!WriteFile(handle, bytes + written, chunk, &count, NULL) || count == 0)
        {
            set_windows_error(ctx, "Failed to write file");
            CloseHandle(handle);
            return false;
        }
        written += count;
    }
    if (!CloseHandle(handle))
    {
        set_windows_error(ctx, "Failed to close file");
        return false;
    }
    const char *base = write_base_dir(ctx);
    int path_length = snprintf(ctx->last_write_path, sizeof(ctx->last_write_path),
                               "%s/%s", base, rel_path);
    if (path_length < 0 || (size_t)path_length >= sizeof(ctx->last_write_path))
    {
        ctx->last_write_path[0] = '\0';
        snprintf(ctx->error, sizeof(ctx->error), "Written file path is too long");
        return false;
    }
    clear_error(ctx);
    return true;
#else
    if (len > 0 && !data)
        return false;
    int fd = open_write_file(ctx, rel_path, append);
    if (fd < 0)
        return false;
    const uint8_t *bytes = data;
    size_t written = 0;
    while (written < len)
    {
        ssize_t count = write(fd, bytes + written, len - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            snprintf(ctx->error, sizeof(ctx->error), "Failed to write file: %s", strerror(errno));
            close(fd);
            return false;
        }
        written += (size_t)count;
    }
    if (close(fd) != 0)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Failed to close file: %s", strerror(errno));
        return false;
    }

    const char *base = write_base_dir(ctx);
    int path_length = snprintf(ctx->last_write_path, sizeof(ctx->last_write_path),
                               "%s/%s", base, rel_path);
    if (path_length < 0 || (size_t)path_length >= sizeof(ctx->last_write_path))
    {
        ctx->last_write_path[0] = '\0';
        snprintf(ctx->error, sizeof(ctx->error), "Written file path is too long");
        return false;
    }

#ifdef __EMSCRIPTEN__
    budo_web_download_file(path_basename(ctx->last_write_path), (const char *)data, (int)len,
                           "application/octet-stream");
    EM_ASM({
        FS.syncfs(false, function(err) {
            if (err) console.warn('[budo-web] sys.files persistence error:', err); });
    });
#endif
    clear_error(ctx);
    return true;
#endif
}

bool file_write_text(FileContext *ctx, const char *rel_path, const char *text, size_t len)
{
    FileRoot root = FILE_ROOT_FILES;
    const char *relative;
    if (!ctx || !rel_path || !resolve_virtual_path(ctx, rel_path, &root, &relative) ||
        root != FILE_ROOT_FILES || !relative[0])
    {
        if (ctx && root == FILE_ROOT_ASSETS)
            snprintf(ctx->error, sizeof(ctx->error), "assets/ is read-only");
        return false;
    }
    return file_write_bytes(ctx, relative, text, len, false);
}

bool file_write_binary(FileContext *ctx, const char *rel_path, const uint8_t *data, size_t len)
{
    FileRoot root = FILE_ROOT_FILES;
    const char *relative;
    if (!ctx || !rel_path || !resolve_virtual_path(ctx, rel_path, &root, &relative) ||
        root != FILE_ROOT_FILES || !relative[0])
    {
        if (ctx && root == FILE_ROOT_ASSETS)
            snprintf(ctx->error, sizeof(ctx->error), "assets/ is read-only");
        return false;
    }
    return file_write_bytes(ctx, relative, data, len, false);
}

bool file_append_binary(FileContext *ctx, const char *rel_path,
                        const uint8_t *data, size_t len)
{
    FileRoot root = FILE_ROOT_FILES;
    const char *relative;
    if (!ctx || !rel_path || !resolve_virtual_path(ctx, rel_path, &root, &relative) ||
        root != FILE_ROOT_FILES || !relative[0])
    {
        if (ctx && root == FILE_ROOT_ASSETS)
            snprintf(ctx->error, sizeof(ctx->error), "assets/ is read-only");
        return false;
    }
    return file_write_bytes(ctx, relative, data, len, true);
}

const char *file_get_last_write_path(FileContext *ctx)
{
    if (!ctx)
        return "";
    return ctx->last_write_path;
}

const char *file_get_error(FileContext *ctx)
{
    if (!ctx)
        return "";
    return ctx->error;
}