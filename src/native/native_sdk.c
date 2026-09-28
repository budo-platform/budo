#include "native_sdk.h"

#include "budo/version.h"
#include "native/native_build_config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define getpid _getpid
#define lstat stat
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#ifndef S_ISLNK
#define S_ISLNK(mode) 0
#endif
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define SDK_MAX_METADATA (16U * 1024U * 1024U)
#define SDK_MAX_MEMBERS 10000U
#define SDK_MAX_MEMBER_SIZE (512ULL * 1024ULL * 1024ULL)
#define SDK_MAX_TOTAL_SIZE (2ULL * 1024ULL * 1024ULL * 1024ULL)

typedef struct SdkFile
{
    char *path;
    uint64_t size;
    char digest[65];
} SdkFile;

typedef struct SdkFileList
{
    SdkFile *items;
    size_t count;
    size_t capacity;
} SdkFileList;

typedef struct ReleaseArtifact
{
    char filename[512];
    char url[1024];
    char archive_digest[65];
    char manifest_digest[65];
    char input_digest[65];
    uint64_t size;
} ReleaseArtifact;

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list args;
    if (!error || !size)
        return;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

static bool regular_file(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool directory_exists(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool valid_digest(const char *text)
{
    size_t i;
    if (!text || strlen(text) != 64)
        return false;
    for (i = 0; i < 64; i++)
        if (!((text[i] >= '0' && text[i] <= '9') ||
              (text[i] >= 'a' && text[i] <= 'f')))
            return false;
    return true;
}

static bool read_file(const char *path, size_t maximum, char **contents,
                      size_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *buffer;
    if (!file || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < 0 || (size_t)size > maximum ||
        fseek(file, 0, SEEK_SET) != 0)
    {
        if (file)
            fclose(file);
        return false;
    }
    buffer = malloc((size_t)size + 1);
    if (!buffer || fread(buffer, 1, (size_t)size, file) != (size_t)size ||
        fclose(file) != 0)
    {
        free(buffer);
        return false;
    }
    buffer[size] = '\0';
    *contents = buffer;
    if (length)
        *length = (size_t)size;
    return true;
}

static bool copy_file(const char *source, const char *destination)
{
    FILE *in = fopen(source, "rb"), *out;
    unsigned char buffer[32768];
    size_t amount;
    if (!in)
        return false;
    out = fopen(destination, "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }
    while ((amount = fread(buffer, 1, sizeof(buffer), in)) != 0)
        if (fwrite(buffer, 1, amount, out) != amount)
        {
            fclose(in);
            fclose(out);
            unlink(destination);
            return false;
        }
    if (ferror(in) || fclose(in) != 0 || fclose(out) != 0)
    {
        unlink(destination);
        return false;
    }
    return true;
}

static bool remove_tree(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    bool ok = true;
    if (!directory)
        return errno == ENOENT;
    while ((entry = readdir(directory)) != NULL)
    {
        char child[BUDO_NATIVE_CACHE_PATH_SIZE];
        struct stat info;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
                (int)sizeof(child) ||
            lstat(child, &info) != 0)
            ok = false;
        else if (S_ISDIR(info.st_mode))
            ok = remove_tree(child) && ok;
        else if (unlink(child) != 0)
            ok = false;
    }
    closedir(directory);
    return rmdir(path) == 0 && ok;
}

static bool list_add(SdkFileList *list, const char *path, uint64_t size,
                     const char *digest)
{
    SdkFile *grown;
    size_t i;
    for (i = 0; i < list->count; i++)
        if (!strcmp(list->items[i].path, path))
            return false;
    if (list->count == list->capacity)
    {
        size_t capacity = list->capacity ? list->capacity * 2 : 64;
        grown = realloc(list->items, capacity * sizeof(*grown));
        if (!grown)
            return false;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count].path = malloc(strlen(path) + 1);
    if (!list->items[list->count].path)
        return false;
    strcpy(list->items[list->count].path, path);
    list->items[list->count].size = size;
    snprintf(list->items[list->count].digest,
             sizeof(list->items[list->count].digest), "%s", digest ? digest : "");
    list->count++;
    return true;
}

static void list_free(SdkFileList *list)
{
    size_t i;
    for (i = 0; i < list->count; i++)
        free(list->items[i].path);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static const char *skip_space(const char *cursor)
{
    while (*cursor && isspace((unsigned char)*cursor))
        cursor++;
    return cursor;
}

static bool object_string(const char *begin, const char *end, const char *key,
                          char *output, size_t size, bool required)
{
    char pattern[128];
    const char *at, *finish;
    if (snprintf(pattern, sizeof(pattern), "\"%s\":", key) >= (int)sizeof(pattern))
        return false;
    at = strstr(begin, pattern);
    if (!at || at >= end)
        return !required && (output[0] = '\0', true);
    at = skip_space(at + strlen(pattern));
    if (at >= end || *at++ != '"')
        return false;
    finish = strchr(at, '"');
    if (!finish || finish > end || memchr(at, '\\', (size_t)(finish - at)) ||
        (size_t)(finish - at) >= size)
        return false;
    memcpy(output, at, (size_t)(finish - at));
    output[finish - at] = '\0';
    return true;
}

static bool object_u64(const char *begin, const char *end, const char *key,
                       uint64_t *value)
{
    char pattern[128], number[32];
    const char *at, *finish;
    char *parsed;
    unsigned long long result;
    if (snprintf(pattern, sizeof(pattern), "\"%s\":", key) >= (int)sizeof(pattern))
        return false;
    at = strstr(begin, pattern);
    if (!at || at >= end)
        return false;
    at = skip_space(at + strlen(pattern));
    finish = at;
    while (finish < end && isdigit((unsigned char)*finish))
        finish++;
    if (finish == at || (size_t)(finish - at) >= sizeof(number))
        return false;
    memcpy(number, at, (size_t)(finish - at));
    number[finish - at] = '\0';
    errno = 0;
    result = strtoull(number, &parsed, 10);
    if (errno || !parsed || *parsed)
        return false;
    *value = (uint64_t)result;
    return true;
}

static bool safe_relative_path(const char *path)
{
    const char *part = path;
    if (!path[0] || path[0] == '/' || path[0] == '\\' ||
        (isalpha((unsigned char)path[0]) && path[1] == ':') || strchr(path, '\\'))
        return false;
    while (*part)
    {
        const char *slash = strchr(part, '/');
        size_t length = slash ? (size_t)(slash - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.'))
            return false;
        part = slash ? slash + 1 : part + length;
    }
    return strlen(path) < BUDO_NATIVE_CACHE_PATH_SIZE / 2;
}

static bool parse_manifest(const char *json, SdkFileList *files,
                           NativeSdkSelection *selection, char *error,
                           size_t error_size)
{
    const char *array, *cursor;
    char version[64], api[32];
    uint64_t schema;
    if (!object_u64(json, json + strlen(json), "schema_version", &schema) || schema != 1 ||
        !object_string(json, json + strlen(json), "budo_version", version,
                       sizeof(version), true) ||
        !object_string(json, json + strlen(json), "native_api_version", api,
                       sizeof(api), true) ||
        !object_string(json, json + strlen(json), "target_tuple",
                       selection->target_tuple, sizeof(selection->target_tuple), true) ||
        !object_string(json, json + strlen(json), "compiler_family",
                       selection->compiler_family,
                       sizeof(selection->compiler_family), true) ||
        !object_string(json, json + strlen(json), "sdk_input_digest",
                       selection->sdk_input_digest,
                       sizeof(selection->sdk_input_digest), true) ||
        strcmp(version, BUDO_VERSION_STRING) || strcmp(api, "1.0") ||
        strcmp(selection->target_tuple, BUDO_LOCAL_TARGET_TUPLE) ||
        !valid_digest(selection->sdk_input_digest))
    {
        set_error(error, error_size,
                  "SDK inner manifest has incompatible or invalid identity");
        return false;
    }
    array = strstr(json, "\"files\":[");
    if (!array)
    {
        set_error(error, error_size, "SDK inner manifest has no complete file list");
        return false;
    }
    cursor = array + strlen("\"files\":[");
    for (;;)
    {
        const char *end;
        char path[BUDO_NATIVE_CACHE_PATH_SIZE], digest[65];
        uint64_t size;
        cursor = skip_space(cursor);
        if (*cursor == ']')
            break;
        if (*cursor != '{' || !(end = strchr(cursor, '}')) ||
            !object_string(cursor, end, "path", path, sizeof(path), true) ||
            !object_u64(cursor, end, "size", &size) ||
            !object_string(cursor, end, "sha256", digest, sizeof(digest), true) ||
            !safe_relative_path(path) || !valid_digest(digest) ||
            size > SDK_MAX_MEMBER_SIZE || files->count >= SDK_MAX_MEMBERS ||
            !list_add(files, path, size, digest))
        {
            set_error(error, error_size,
                      "SDK inner manifest contains an invalid or duplicate file entry");
            return false;
        }
        cursor = skip_space(end + 1);
        if (*cursor == ',')
            cursor++;
        else if (*cursor != ']')
        {
            set_error(error, error_size, "SDK inner manifest file list is malformed");
            return false;
        }
    }
    if (!files->count)
    {
        set_error(error, error_size, "SDK inner manifest file list is empty");
        return false;
    }
    return true;
}

static bool collect_tree(const char *root, const char *relative,
                         SdkFileList *files, char *error, size_t error_size)
{
    char directory_path[BUDO_NATIVE_CACHE_PATH_SIZE];
    DIR *directory;
    struct dirent *entry;
    if (relative[0])
        snprintf(directory_path, sizeof(directory_path), "%s/%s", root, relative);
    else
        snprintf(directory_path, sizeof(directory_path), "%s", root);
    directory = opendir(directory_path);
    if (!directory)
        return false;
    while ((entry = readdir(directory)) != NULL)
    {
        char child_relative[BUDO_NATIVE_CACHE_PATH_SIZE];
        char child[BUDO_NATIVE_CACHE_PATH_SIZE];
        struct stat info;
        char digest[65];
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (snprintf(child_relative, sizeof(child_relative), "%s%s%s", relative,
                     relative[0] ? "/" : "", entry->d_name) >=
                (int)sizeof(child_relative) ||
            snprintf(child, sizeof(child), "%s/%s", root, child_relative) >=
                (int)sizeof(child) ||
            lstat(child, &info) != 0)
        {
            closedir(directory);
            return false;
        }
        if (S_ISLNK(info.st_mode) || (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)))
        {
            set_error(error, error_size, "SDK contains a link or special member: %s",
                      child_relative);
            closedir(directory);
            return false;
        }
        if (S_ISDIR(info.st_mode))
        {
            if (!collect_tree(root, child_relative, files, error, error_size))
            {
                closedir(directory);
                return false;
            }
        }
        else if (strcmp(child_relative, "share/budo/budo-native-sdk.json"))
        {
            if ((uint64_t)info.st_size > SDK_MAX_MEMBER_SIZE ||
                !native_cache_file_digest(child, digest) ||
                files->count >= SDK_MAX_MEMBERS ||
                !list_add(files, child_relative, (uint64_t)info.st_size, digest))
            {
                closedir(directory);
                return false;
            }
        }
    }
    closedir(directory);
    return true;
}

static int compare_files(const void *left, const void *right)
{
    const SdkFile *a = left, *b = right;
    return strcmp(a->path, b->path);
}

bool native_sdk_verify_directory(const char *directory,
                                 NativeSdkSelection *selection,
                                 char *error, size_t error_size)
{
    char manifest[BUDO_NATIVE_CACHE_PATH_SIZE];
    char *json = NULL;
    SdkFileList expected = {0}, actual = {0};
    size_t i;
    bool ok = false;
    NativeSdkSelection parsed = {0};
    if (!directory ||
        snprintf(manifest, sizeof(manifest), "%s/share/budo/budo-native-sdk.json",
                 directory) >= (int)sizeof(manifest) ||
        !regular_file(manifest) || !read_file(manifest, SDK_MAX_METADATA, &json, NULL) ||
        !native_cache_file_digest(manifest, parsed.manifest_sha256))
    {
        set_error(error, error_size, "SDK is missing a readable inner manifest");
        goto done;
    }
    if (!parse_manifest(json, &expected, &parsed, error, error_size) ||
        !collect_tree(directory, "", &actual, error, error_size))
        goto done;
    qsort(expected.items, expected.count, sizeof(*expected.items), compare_files);
    qsort(actual.items, actual.count, sizeof(*actual.items), compare_files);
    if (expected.count != actual.count)
    {
        set_error(error, error_size,
                  "SDK contents do not exactly match the inner manifest (%zu expected, %zu found)",
                  expected.count, actual.count);
        goto done;
    }
    for (i = 0; i < expected.count; i++)
        if (strcmp(expected.items[i].path, actual.items[i].path) ||
            expected.items[i].size != actual.items[i].size ||
            strcmp(expected.items[i].digest, actual.items[i].digest))
        {
            set_error(error, error_size, "SDK member failed integrity verification: %s",
                      expected.items[i].path);
            goto done;
        }
    if (selection)
    {
        char source[sizeof(selection->source)];
        char release[65], archive[65];
        bool downloaded = selection->downloaded;
        snprintf(source, sizeof(source), "%s", selection->source);
        snprintf(release, sizeof(release), "%s", selection->release_metadata_sha256);
        snprintf(archive, sizeof(archive), "%s", selection->archive_sha256);
        *selection = parsed;
        snprintf(selection->directory, sizeof(selection->directory), "%s", directory);
        snprintf(selection->source, sizeof(selection->source), "%s", source);
        snprintf(selection->release_metadata_sha256,
                 sizeof(selection->release_metadata_sha256), "%s", release);
        snprintf(selection->archive_sha256, sizeof(selection->archive_sha256), "%s", archive);
        selection->downloaded = downloaded;
    }
    ok = true;
done:
    free(json);
    list_free(&expected);
    list_free(&actual);
    return ok;
}

bool native_sdk_use_directory(const char *directory, const char *source,
                              NativeSdkSelection *selection,
                              char *error, size_t error_size)
{
    memset(selection, 0, sizeof(*selection));
    snprintf(selection->source, sizeof(selection->source), "%s", source);
    if (!native_sdk_verify_directory(directory, selection, error, error_size))
        return false;
    return true;
}

static bool tar_checksum(const unsigned char block[512])
{
    uint64_t stored = 0, sum = 0;
    size_t i;
    for (i = 148; i < 156; i++)
    {
        if (block[i] == ' ' || block[i] == '\0')
            continue;
        if (block[i] < '0' || block[i] > '7')
            return false;
        stored = stored * 8 + (unsigned)(block[i] - '0');
    }
    for (i = 0; i < 512; i++)
        sum += (i >= 148 && i < 156) ? ' ' : block[i];
    return stored == sum;
}

static bool tar_octal(const unsigned char *text, size_t length, uint64_t *value)
{
    size_t i = 0;
    uint64_t result = 0;
    while (i < length && (text[i] == ' ' || text[i] == '\0'))
        i++;
    if (i == length)
    {
        *value = 0;
        return true;
    }
    for (; i < length && text[i] != '\0' && text[i] != ' '; i++)
    {
        if (text[i] < '0' || text[i] > '7' || result > (UINT64_MAX >> 3))
            return false;
        result = (result << 3) + (unsigned)(text[i] - '0');
    }
    *value = result;
    return true;
}

static bool gzip_read_exact(gzFile stream, void *data, size_t size)
{
    unsigned char *cursor = data;
    while (size)
    {
        unsigned amount = size > 1024U * 1024U ? 1024U * 1024U : (unsigned)size;
        int got = gzread(stream, cursor, amount);
        if (got <= 0)
            return false;
        cursor += got;
        size -= (size_t)got;
    }
    return true;
}

static bool ensure_parents(const char *root, const char *relative)
{
    char path[BUDO_NATIVE_CACHE_PATH_SIZE];
    char *cursor;
    struct stat info;
    if (snprintf(path, sizeof(path), "%s/%s", root, relative) >= (int)sizeof(path))
        return false;
    cursor = path + strlen(root) + 1;
    while ((cursor = strchr(cursor, '/')) != NULL)
    {
        *cursor = '\0';
        if (lstat(path, &info) == 0)
        {
            if (!S_ISDIR(info.st_mode))
                return false;
        }
        else if (mkdir(path, 0755) != 0)
            return false;
        *cursor++ = '/';
    }
    return true;
}

bool native_sdk_extract_tar_gz(const char *archive, const char *destination,
                               char *error, size_t error_size)
{
    gzFile stream;
    unsigned char block[512], padding[512];
    SdkFileList names = {0};
    char prefix[BUDO_NATIVE_CACHE_PATH_SIZE] = "";
    uint64_t total = 0;
    size_t count = 0;
    bool ok = false;
    int zero_blocks = 0;
    if (directory_exists(destination))
        remove_tree(destination);
    if (!native_cache_make_directories(destination) || !(stream = gzopen(archive, "rb")))
    {
        set_error(error, error_size, "cannot open SDK archive");
        return false;
    }
    for (;;)
    {
        char full[BUDO_NATIVE_CACHE_PATH_SIZE], relative[BUDO_NATIVE_CACHE_PATH_SIZE];
        char *slash;
        uint64_t size, mode;
        char type;
        bool zero = true;
        size_t i;
        FILE *output = NULL;
        if (!gzip_read_exact(stream, block, sizeof(block)))
        {
            set_error(error, error_size, "truncated SDK tar archive");
            goto done;
        }
        for (i = 0; i < sizeof(block); i++)
            if (block[i])
            {
                zero = false;
                break;
            }
        if (zero)
        {
            if (++zero_blocks == 2)
            {
                ok = true;
                break;
            }
            continue;
        }
        zero_blocks = 0;
        if (!tar_checksum(block) || memchr(block, '\0', 100) == NULL ||
            !tar_octal(block + 124, 12, &size) || !tar_octal(block + 100, 8, &mode))
        {
            set_error(error, error_size, "invalid SDK tar header");
            goto done;
        }
        type = (char)block[156];
        if (block[345])
        {
            if (memchr(block + 345, '\0', 155) == NULL ||
                snprintf(full, sizeof(full), "%s/%s", block + 345, block) >=
                    (int)sizeof(full))
            {
                set_error(error, error_size, "overlong SDK tar path");
                goto done;
            }
        }
        else
            snprintf(full, sizeof(full), "%s", block);
        if (!safe_relative_path(full) || !(slash = strchr(full, '/')) || slash == full)
        {
            set_error(error, error_size, "unsafe SDK archive path: %s", full);
            goto done;
        }
        *slash = '\0';
        if (!prefix[0])
            snprintf(prefix, sizeof(prefix), "%s", full);
        if (strcmp(prefix, full))
        {
            set_error(error, error_size, "SDK archive has multiple top-level roots");
            goto done;
        }
        snprintf(relative, sizeof(relative), "%s", slash + 1);
        if (!safe_relative_path(relative) || count++ >= SDK_MAX_MEMBERS ||
            size > SDK_MAX_MEMBER_SIZE || total > SDK_MAX_TOTAL_SIZE - size ||
            !list_add(&names, relative, size, ""))
        {
            set_error(error, error_size, "duplicate, unsafe, or oversized SDK member: %s",
                      relative);
            goto done;
        }
        total += size;
        if (type != '\0' && type != '0' && type != '5')
        {
            set_error(error, error_size,
                      "SDK archive links and special members are forbidden: %s", relative);
            goto done;
        }
        if ((type == '5' && size != 0) || !ensure_parents(destination, relative))
        {
            set_error(error, error_size, "invalid SDK directory member: %s", relative);
            goto done;
        }
        if (type == '5')
        {
            char path[BUDO_NATIVE_CACHE_PATH_SIZE];
            struct stat info;
            snprintf(path, sizeof(path), "%s/%s", destination, relative);
            if (lstat(path, &info) == 0 ? !S_ISDIR(info.st_mode) : mkdir(path, 0755) != 0)
            {
                set_error(error, error_size, "cannot create SDK directory: %s", relative);
                goto done;
            }
        }
        else
        {
            char path[BUDO_NATIVE_CACHE_PATH_SIZE];
            uint64_t remaining = size;
            snprintf(path, sizeof(path), "%s/%s", destination, relative);
            if (regular_file(path) || directory_exists(path) || !(output = fopen(path, "wb")))
            {
                set_error(error, error_size, "duplicate or unwritable SDK member: %s", relative);
                goto done;
            }
            while (remaining)
            {
                size_t amount = remaining > sizeof(padding) ? sizeof(padding) : (size_t)remaining;
                if (!gzip_read_exact(stream, padding, amount) ||
                    fwrite(padding, 1, amount, output) != amount)
                {
                    fclose(output);
                    set_error(error, error_size, "truncated SDK member: %s", relative);
                    goto done;
                }
                remaining -= amount;
            }
            if (fclose(output) != 0)
                goto done;
#ifndef _WIN32
            chmod(path, (mode & 0111) ? 0755 : 0644);
#endif
        }
        if (size % 512 && !gzip_read_exact(stream, padding, 512 - (size_t)(size % 512)))
        {
            set_error(error, error_size, "truncated SDK member padding");
            goto done;
        }
    }
done:
    gzclose(stream);
    list_free(&names);
    if (!ok)
        remove_tree(destination);
    return ok;
}

static bool sdk_cache_root(char *path, size_t size)
{
    const char *override = getenv("BUDO_NATIVE_SDK_CACHE_DIR");
    const char *home;
    int length;
    if (override && override[0])
        length = snprintf(path, size, "%s", override);
#ifdef _WIN32
    else
    {
        home = getenv("LOCALAPPDATA");
        if (!home)
            home = getenv("USERPROFILE");
        length = home ? snprintf(path, size, "%s/Budo/native-sdks", home) : -1;
    }
#else
    else
    {
        const char *xdg = getenv("XDG_CACHE_HOME");
        home = getenv("HOME");
        length = xdg && xdg[0] ? snprintf(path, size, "%s/budo/native-sdks", xdg)
                               : (home ? snprintf(path, size,
                                                  "%s/.cache/budo/native-sdks", home)
                                       : -1);
    }
#endif
    return length >= 0 && (size_t)length < size;
}

static int run_process(const char *const arguments[])
{
#ifdef _WIN32
    intptr_t result = _spawnvp(_P_WAIT, arguments[0], (const char *const *)arguments);
    return result == -1 ? 127 : (int)result;
#else
    pid_t child = fork();
    int status;
    if (child < 0)
        return 127;
    if (!child)
    {
        execvp(arguments[0], (char *const *)arguments);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            return 127;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif
}

static bool file_url_path(const char *url, char *path, size_t size)
{
    const char *source;
    size_t out = 0;
    if (strncmp(url, "file://", 7))
        return false;
    source = url + 7;
    while (*source && out + 1 < size)
    {
        if (source[0] == '%' && source[1] && source[2])
        {
            int high = isdigit((unsigned char)source[1]) ? source[1] - '0'
                                                         : tolower((unsigned char)source[1]) - 'a' + 10;
            int low = isdigit((unsigned char)source[2]) ? source[2] - '0'
                                                        : tolower((unsigned char)source[2]) - 'a' + 10;
            if (high < 0 || high > 15 || low < 0 || low > 15)
                return false;
            path[out++] = (char)((high << 4) | low);
            source += 3;
        }
        else
            path[out++] = *source++;
    }
    path[out] = '\0';
    return !*source;
}

static bool download(const char *url, const char *destination,
                     char *error, size_t error_size)
{
    char local[BUDO_NATIVE_CACHE_PATH_SIZE];
    if (file_url_path(url, local, sizeof(local)))
    {
        if (!copy_file(local, destination))
        {
            set_error(error, error_size, "cannot copy %s", url);
            return false;
        }
        return true;
    }
    {
        const char *cmake = regular_file(BUDO_LOCAL_CMAKE_COMMAND)
                                ? BUDO_LOCAL_CMAKE_COMMAND
                                : "cmake";
        char script[BUDO_NATIVE_CACHE_PATH_SIZE];
        char url_argument[1400], output_argument[1400];
        FILE *file;
        snprintf(script, sizeof(script), "%s.download.cmake", destination);
        file = fopen(script, "wb");
        if (!file || fputs("file(DOWNLOAD \"${URL}\" \"${OUTPUT}\" TLS_VERIFY ON STATUS status)\nlist(GET status 0 code)\nif(NOT code EQUAL 0)\n message(FATAL_ERROR \"download failed: ${status}\")\nendif()\n", file) < 0 || fclose(file) != 0)
        {
            if (file)
                fclose(file);
            set_error(error, error_size, "cannot prepare SDK download");
            return false;
        }
        snprintf(url_argument, sizeof(url_argument), "-DURL=%s", url);
        snprintf(output_argument, sizeof(output_argument), "-DOUTPUT=%s", destination);
        {
            const char *arguments[] = {cmake, url_argument, output_argument, "-P", script, NULL};
            int result = run_process(arguments);
            unlink(script);
            if (result != 0)
            {
                set_error(error, error_size, "SDK download failed for %s", url);
                return false;
            }
        }
    }
    return true;
}

static bool parse_release(const char *json, ReleaseArtifact *artifact,
                          char *error, size_t error_size)
{
    const char *array = strstr(json, "\"artifacts\":[");
    const char *cursor;
    char version[64];
    uint64_t schema;
    if (!array || !object_u64(json, json + strlen(json), "schema_version", &schema) ||
        schema != 1 || !object_string(json, json + strlen(json), "budo_version", version, sizeof(version), true) ||
        strcmp(version, BUDO_VERSION_STRING))
    {
        set_error(error, error_size, "release metadata has an incompatible identity");
        return false;
    }
    cursor = array + strlen("\"artifacts\":[");
    while (*cursor && *cursor != ']')
    {
        const char *end;
        char tuple[128];
        cursor = skip_space(cursor);
        if (*cursor != '{' || !(end = strchr(cursor, '}')) ||
            !object_string(cursor, end, "target_tuple", tuple, sizeof(tuple), true))
            break;
        if (!strcmp(tuple, BUDO_LOCAL_TARGET_TUPLE))
        {
            if (!object_string(cursor, end, "filename", artifact->filename,
                               sizeof(artifact->filename), true) ||
                !object_string(cursor, end, "url", artifact->url,
                               sizeof(artifact->url), false) ||
                !object_string(cursor, end, "sha256", artifact->archive_digest,
                               sizeof(artifact->archive_digest), true) ||
                !object_string(cursor, end, "sdk_manifest_sha256",
                               artifact->manifest_digest,
                               sizeof(artifact->manifest_digest), true) ||
                !object_string(cursor, end, "sdk_input_digest",
                               artifact->input_digest,
                               sizeof(artifact->input_digest), true) ||
                !object_u64(cursor, end, "size", &artifact->size) ||
                !valid_digest(artifact->archive_digest) ||
                !valid_digest(artifact->manifest_digest) ||
                !valid_digest(artifact->input_digest) ||
                !safe_relative_path(artifact->filename) || strchr(artifact->filename, '/'))
                break;
            return true;
        }
        cursor = end + 1;
        cursor = skip_space(cursor);
        if (*cursor == ',')
            cursor++;
    }
    set_error(error, error_size, "release metadata has no valid artifact for %s",
              BUDO_LOCAL_TARGET_TUPLE);
    return false;
}

static bool join_archive_url(const char *metadata_url, const ReleaseArtifact *artifact,
                             char *url, size_t size)
{
    const char *base = getenv("BUDO_NATIVE_SDK_ARCHIVE_BASE_URL");
    const char *slash;
    if (artifact->url[0])
        return snprintf(url, size, "%s", artifact->url) < (int)size;
    if (base && base[0])
        return snprintf(url, size, "%s%s%s", base,
                        base[strlen(base) - 1] == '/' ? "" : "/",
                        artifact->filename) < (int)size;
    slash = strrchr(metadata_url, '/');
    if (!slash)
        return false;
    return snprintf(url, size, "%.*s/%s", (int)(slash - metadata_url), metadata_url,
                    artifact->filename) < (int)size;
}

bool native_sdk_resolve(bool offline, NativeSdkSelection *selection,
                        char *error, size_t error_size)
{
    const char *metadata_url = getenv("BUDO_NATIVE_SDK_RELEASE_METADATA_URL");
    const char *metadata_pin = getenv("BUDO_NATIVE_SDK_RELEASE_METADATA_SHA256");
    char root[BUDO_NATIVE_CACHE_PATH_SIZE], metadata_dir[BUDO_NATIVE_CACHE_PATH_SIZE];
    char metadata_path[BUDO_NATIVE_CACHE_PATH_SIZE], metadata_temp[BUDO_NATIVE_CACHE_PATH_SIZE];
    char actual[65], *json = NULL, archive_url[1400];
    char entry[BUDO_NATIVE_CACHE_PATH_SIZE], sdk[BUDO_NATIVE_CACHE_PATH_SIZE];
    char archive[BUDO_NATIVE_CACHE_PATH_SIZE], complete[BUDO_NATIVE_CACHE_PATH_SIZE];
    char lock_path[BUDO_NATIVE_CACHE_PATH_SIZE], temporary[BUDO_NATIVE_CACHE_PATH_SIZE];
    NativeCacheLock lock;
    ReleaseArtifact artifact = {0};
    struct stat archive_info;
    bool locked = false, ok = false;
    if (!metadata_url || !metadata_url[0])
        metadata_url = BUDO_NATIVE_SDK_RELEASE_METADATA_URL;
    if (!metadata_pin || !metadata_pin[0])
        metadata_pin = BUDO_NATIVE_SDK_RELEASE_METADATA_SHA256;
    if (!metadata_url[0] || !valid_digest(metadata_pin))
    {
        set_error(error, error_size,
                  "automatic SDK resolution requires independently pinned release metadata");
        return false;
    }
    if (!sdk_cache_root(root, sizeof(root)) ||
        snprintf(metadata_dir, sizeof(metadata_dir), "%s/metadata", root) >=
            (int)sizeof(metadata_dir) ||
        !native_cache_make_directories(metadata_dir) ||
        snprintf(metadata_path, sizeof(metadata_path), "%s/%s.json", metadata_dir,
                 metadata_pin) >= (int)sizeof(metadata_path))
    {
        set_error(error, error_size, "cannot initialize native SDK cache");
        return false;
    }
    if (!regular_file(metadata_path))
    {
        if (offline)
        {
            set_error(error, error_size, "offline: pinned SDK release metadata is not cached");
            return false;
        }
        snprintf(metadata_temp, sizeof(metadata_temp), "%s.tmp.%ld", metadata_path,
                 (long)getpid());
        if (!download(metadata_url, metadata_temp, error, error_size) ||
            !native_cache_file_digest(metadata_temp, actual) || strcmp(actual, metadata_pin))
        {
            unlink(metadata_temp);
            if (!error || !error_size || !error[0])
                set_error(error, error_size,
                          "release metadata does not match its independent SHA-256 pin");
            return false;
        }
        if (rename(metadata_temp, metadata_path) != 0 && errno != EEXIST)
        {
            unlink(metadata_temp);
            set_error(error, error_size, "cannot cache pinned release metadata");
            return false;
        }
        unlink(metadata_temp);
    }
    if (!native_cache_file_digest(metadata_path, actual) || strcmp(actual, metadata_pin))
    {
        unlink(metadata_path);
        if (offline)
        {
            set_error(error, error_size,
                      "offline: cached SDK release metadata failed its SHA-256 pin");
            goto done;
        }
        return native_sdk_resolve(false, selection, error, error_size);
    }
    if (!read_file(metadata_path, SDK_MAX_METADATA, &json, NULL) ||
        !parse_release(json, &artifact, error, error_size))
        goto done;
    free(json);
    json = NULL;
    if (!join_archive_url(metadata_url, &artifact, archive_url, sizeof(archive_url)) ||
        snprintf(entry, sizeof(entry), "%s/sha256/%s", root,
                 artifact.archive_digest) >= (int)sizeof(entry) ||
        snprintf(sdk, sizeof(sdk), "%s/sdk", entry) >= (int)sizeof(sdk) ||
        snprintf(archive, sizeof(archive), "%s/archive.tar.gz", entry) >=
            (int)sizeof(archive) ||
        snprintf(complete, sizeof(complete), "%s/complete", entry) >=
            (int)sizeof(complete) ||
        snprintf(lock_path, sizeof(lock_path), "%s/locks/%s.lock", root,
                 artifact.archive_digest) >= (int)sizeof(lock_path) ||
        !native_cache_lock_acquire(lock_path, &lock))
    {
        set_error(error, error_size, "cannot lock native SDK cache entry");
        goto done;
    }
    locked = true;
    memset(selection, 0, sizeof(*selection));
    snprintf(selection->source, sizeof(selection->source), "downloaded");
    snprintf(selection->release_metadata_sha256,
             sizeof(selection->release_metadata_sha256), "%s", metadata_pin);
    snprintf(selection->archive_sha256, sizeof(selection->archive_sha256), "%s",
             artifact.archive_digest);
    selection->downloaded = true;
    if (regular_file(complete) &&
        native_sdk_verify_directory(sdk, selection, error, error_size) &&
        !strcmp(selection->manifest_sha256, artifact.manifest_digest) &&
        !strcmp(selection->sdk_input_digest, artifact.input_digest))
    {
        ok = true;
        goto done;
    }
    if (error && error_size)
        error[0] = '\0';
    if (directory_exists(sdk))
        remove_tree(sdk);
    if (regular_file(archive) &&
        (stat(archive, &archive_info) != 0 || (uint64_t)archive_info.st_size != artifact.size ||
         !native_cache_file_digest(archive, actual) || strcmp(actual, artifact.archive_digest)))
        unlink(archive);
    if (!regular_file(archive))
    {
        char archive_temp[BUDO_NATIVE_CACHE_PATH_SIZE];
        if (offline)
        {
            set_error(error, error_size, "offline: verified SDK archive is not cached");
            goto done;
        }
        if (!native_cache_make_directories(entry))
            goto done;
        snprintf(archive_temp, sizeof(archive_temp), "%s.tmp.%ld", archive, (long)getpid());
        if (!download(archive_url, archive_temp, error, error_size) ||
            stat(archive_temp, &archive_info) != 0 ||
            (uint64_t)archive_info.st_size != artifact.size ||
            !native_cache_file_digest(archive_temp, actual) ||
            strcmp(actual, artifact.archive_digest))
        {
            unlink(archive_temp);
            if (!error || !error_size || !error[0])
                set_error(error, error_size, "SDK archive failed size or SHA-256 verification");
            goto done;
        }
        if (rename(archive_temp, archive) != 0)
        {
            unlink(archive_temp);
            goto done;
        }
    }
    snprintf(temporary, sizeof(temporary), "%s/sdk.tmp.%ld", entry, (long)getpid());
    remove_tree(temporary);
    if (!native_sdk_extract_tar_gz(archive, temporary, error, error_size))
        goto done;
    snprintf(selection->source, sizeof(selection->source), "downloaded");
    snprintf(selection->release_metadata_sha256,
             sizeof(selection->release_metadata_sha256), "%s", metadata_pin);
    snprintf(selection->archive_sha256, sizeof(selection->archive_sha256), "%s",
             artifact.archive_digest);
    selection->downloaded = true;
    if (!native_sdk_verify_directory(temporary, selection, error, error_size) ||
        strcmp(selection->manifest_sha256, artifact.manifest_digest) ||
        strcmp(selection->sdk_input_digest, artifact.input_digest))
    {
        remove_tree(temporary);
        if (!error || !error_size || !error[0])
            set_error(error, error_size, "SDK inner manifest does not match release metadata");
        goto done;
    }
    if (rename(temporary, sdk) != 0)
    {
        remove_tree(temporary);
        set_error(error, error_size, "cannot atomically install native SDK");
        goto done;
    }
    {
        FILE *marker = fopen(complete, "wb");
        bool marker_ok = marker &&
                         fprintf(marker, "release_metadata_sha256=%s\n", metadata_pin) >= 0;
        if (marker && fclose(marker) != 0)
            marker_ok = false;
        if (!marker_ok)
        {
            remove_tree(sdk);
            goto done;
        }
    }
    snprintf(selection->directory, sizeof(selection->directory), "%s", sdk);
    ok = true;
done:
    free(json);
    if (locked)
        native_cache_lock_release(&lock);
    return ok;
}

static uint64_t tree_size(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    uint64_t total = 0;
    if (!directory)
        return 0;
    while ((entry = readdir(directory)) != NULL)
    {
        char child[BUDO_NATIVE_CACHE_PATH_SIZE];
        struct stat info;
        if (entry->d_name[0] == '.')
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (lstat(child, &info) == 0)
            total += S_ISDIR(info.st_mode) ? tree_size(child) : (uint64_t)info.st_size;
    }
    closedir(directory);
    return total;
}

int native_sdk_cache_inspect(bool json)
{
    char root[BUDO_NATIVE_CACHE_PATH_SIZE], base[BUDO_NATIVE_CACHE_PATH_SIZE];
    DIR *directory;
    struct dirent *entry;
    size_t count = 0;
    uint64_t bytes = 0;
    if (!sdk_cache_root(root, sizeof(root)) ||
        snprintf(base, sizeof(base), "%s/sha256", root) >= (int)sizeof(base))
        return 1;
    directory = opendir(base);
    if (json)
        printf("{\"root\":\"%s\",\"namespace\":\"native-sdks\",\"entries\":[", root);
    else
        printf("Native SDK cache: %s\n", root);
    if (directory)
        while ((entry = readdir(directory)) != NULL)
        {
            char path[BUDO_NATIVE_CACHE_PATH_SIZE], complete[BUDO_NATIVE_CACHE_PATH_SIZE];
            uint64_t size;
            if (entry->d_name[0] == '.')
                continue;
            snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
            snprintf(complete, sizeof(complete), "%s/complete", path);
            if (!regular_file(complete))
                continue;
            size = tree_size(path);
            if (json)
                printf("%s{\"archive_sha256\":\"%s\",\"bytes\":%llu}",
                       count ? "," : "", entry->d_name, (unsigned long long)size);
            else
                printf("  %s  %llu bytes\n", entry->d_name, (unsigned long long)size);
            count++;
            bytes += size;
        }
    if (directory)
        closedir(directory);
    if (json)
        printf("],\"count\":%zu,\"bytes\":%llu}\n", count,
               (unsigned long long)bytes);
    else
        printf("%zu SDK entries, %llu bytes\n", count, (unsigned long long)bytes);
    return 0;
}

int native_sdk_cache_clean_all(void)
{
    char root[BUDO_NATIVE_CACHE_PATH_SIZE], base[BUDO_NATIVE_CACHE_PATH_SIZE];
    DIR *directory;
    struct dirent *entry;
    size_t removed = 0;
    if (!sdk_cache_root(root, sizeof(root)) ||
        snprintf(base, sizeof(base), "%s/sha256", root) >= (int)sizeof(base))
        return 1;
    directory = opendir(base);
    if (!directory)
    {
        printf("Native SDK cache is empty.\n");
        return 0;
    }
    while ((entry = readdir(directory)) != NULL)
    {
        char path[BUDO_NATIVE_CACHE_PATH_SIZE], lock_path[BUDO_NATIVE_CACHE_PATH_SIZE];
        NativeCacheLock lock;
        if (entry->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
        if (snprintf(lock_path, sizeof(lock_path), "%s/locks/%s.lock", root,
                     entry->d_name) >= (int)sizeof(lock_path) ||
            !native_cache_lock_acquire(lock_path, &lock))
            continue;
        if (remove_tree(path))
            removed++;
        native_cache_lock_release(&lock);
    }
    closedir(directory);
    printf("Removed %zu native SDK cache entries; native build results were untouched.\n",
           removed);
    return 0;
}