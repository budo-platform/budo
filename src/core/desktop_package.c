#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#endif

#include "core/desktop_package.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "core/app_entrypoint.h"
#include "core/fs_util.h"
#include "native/native_cache.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define mkdir_private(path) (_mkdir(path) == 0)
#define seek_file _fseeki64
#define tell_file _ftelli64
#define duplicate_file(file) _dup(_fileno(file))
#define close_fd _close
#else
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define mkdir_private(path) (mkdir(path, 0700) == 0)
#define seek_file fseeko
#define tell_file ftello
#define duplicate_file(file) dup(fileno(file))
#define close_fd close
#endif

#ifndef BUDO_PACKAGE_GITHUB_REPOSITORY
#define BUDO_PACKAGE_GITHUB_REPOSITORY "budo-platform/budo"
#endif

#define PATH_SIZE BUDO_PACKAGE_PATH_SIZE
#define ID_SIZE 64
#define BUFFER_SIZE 32768
#define MAX_ENTRIES 100000u
#define MAX_BYTES (UINT64_C(8) * 1024 * 1024 * 1024)
#define MAX_DEPTH 64
#define ENTRYPOINTS (APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA | APP_ENTRYPOINT_WEBASSEMBLY)

static const char format_magic[] = "BUDO-DESKTOP-BUNDLE-V1\x1a";
enum
{
    OFFSET_AT = sizeof(format_magic),
    DIGEST_AT = OFFSET_AT + 8,
    ID_AT = DIGEST_AT + BUDO_NATIVE_CACHE_HEX_SIZE - 1,
    FOOTER_SIZE = ID_AT + ID_SIZE
};

typedef struct
{
    uint64_t offset;
    uint64_t length;
    char digest[BUDO_NATIVE_CACHE_HEX_SIZE];
    char id[ID_SIZE];
} Bundle;

typedef struct
{
    uint64_t bytes;
    unsigned entries;
} ArchiveLimits;

typedef enum { PATH_MISSING, PATH_FILE, PATH_DIRECTORY, PATH_UNSUPPORTED } PathKind;

static bool fits(int length, size_t capacity)
{
    return length >= 0 && (size_t)length < capacity;
}

static bool join(char out[PATH_SIZE], const char *dir, const char *name)
{
    return fits(snprintf(out, PATH_SIZE, "%s/%s", dir, name), PATH_SIZE);
}

static const char *last_separator(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
#endif
    return slash;
}

static PathKind path_kind(const char *path)
{
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                   ? PATH_MISSING : PATH_UNSUPPORTED;
    }
    if (attrs & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
        return PATH_UNSUPPORTED;
    return attrs & FILE_ATTRIBUTE_DIRECTORY ? PATH_DIRECTORY : PATH_FILE;
#else
    struct stat info;
    if (lstat(path, &info) != 0)
        return errno == ENOENT ? PATH_MISSING : PATH_UNSUPPORTED;
    return S_ISDIR(info.st_mode) ? PATH_DIRECTORY :
           S_ISREG(info.st_mode) ? PATH_FILE : PATH_UNSUPPORTED;
#endif
}

static bool make_directory(const char *path)
{
    return mkdir_private(path) || path_kind(path) == PATH_DIRECTORY;
}

static bool make_base(char *path)
{
    struct stat info;
    if (stat(path, &info) == 0)
        return S_ISDIR(info.st_mode);
    if (errno != ENOENT)
        return false;
    char *slash = (char *)last_separator(path);
    if (slash && slash != path
#ifdef _WIN32
        && !(slash == path + 2 && path[1] == ':')
#endif
    )
    {
        char saved = *slash;
        *slash = '\0';
        bool ok = make_base(path);
        *slash = saved;
        if (!ok)
            return false;
    }
    return make_directory(path);
}

static bool self_path(char path[PATH_SIZE])
{
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, path, PATH_SIZE);
    return length > 0 && length < PATH_SIZE;
#elif defined(__APPLE__)
    uint32_t size = PATH_SIZE;
    return _NSGetExecutablePath(path, &size) == 0;
#else
    strcpy(path, "/proc/self/exe");
    return true;
#endif
}

static bool valid_component(const char *s, size_t n)
{
    if (!n || n > 255 || s[n - 1] == '.' || s[n - 1] == ' ')
        return false;
    char stem[8] = {0};
    size_t i = 0;
    for (; i < n && s[i] != '.' && i < sizeof(stem) - 1; ++i)
        stem[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 'a' + 'A' : s[i];
    if (i == n || s[i] == '.')
    {
        if (!strcmp(stem, "CON") || !strcmp(stem, "PRN") ||
            !strcmp(stem, "AUX") || !strcmp(stem, "NUL") ||
            !strcmp(stem, "CONIN$") || !strcmp(stem, "CONOUT$") ||
            ((!strncmp(stem, "COM", 3) || !strncmp(stem, "LPT", 3)) &&
             stem[3] >= '0' && stem[3] <= '9' && !stem[4]))
            return false;
    }
    for (i = 0; i < n; ++i)
        if (s[i] < ' ' || s[i] > '~' || strchr("<>:\"/\\|?*", s[i]))
            return false;
    return true;
}

static bool valid_archive_path(const char *path)
{
    const char *p = path;
    for (unsigned depth = 1; depth <= MAX_DEPTH; ++depth)
    {
        const char *slash = strchr(p, '/');
        if (!valid_component(p, slash ? (size_t)(slash - p) : strlen(p)))
            return false;
        if (!slash)
            return true;
        p = slash + 1;
    }
    return false;
}

static bool valid_id(const char *id)
{
    size_t n = strlen(id);
    if (!n || n >= ID_SIZE || !valid_component(id, n))
        return false;
    for (size_t i = 0; i < n; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') ||
              (i && strchr("._-", id[i]))))
            return false;
    return true;
}

static bool absolute_path(const char *path, char out[PATH_SIZE])
{
#ifdef _WIN32
    return _fullpath(out, path, PATH_SIZE) != NULL;
#else
    char *resolved = realpath(path, NULL);
    if (!resolved)
        return false;
    bool ok = fits(snprintf(out, PATH_SIZE, "%s", resolved), PATH_SIZE);
    free(resolved);
    return ok;
#endif
}

static bool app_id(char id[ID_SIZE], const char *project, const char *name)
{
    if (!name)
    {
        const char *separator = last_separator(project);
        name = separator ? separator + 1 : project;
    }
    if (!fits(snprintf(id, ID_SIZE, "%s", name), ID_SIZE))
        return false;
    for (char *p = id; *p; ++p)
        if (*p >= 'A' && *p <= 'Z')
            *p += 'a' - 'A';
    return valid_id(id);
}

static uint64_t read_le(const unsigned char *p, size_t n)
{
    uint64_t value = 0;
    while (n)
        value = (value << 8) | p[--n];
    return value;
}

static void write_le(unsigned char *p, size_t n, uint64_t value)
{
    for (size_t i = 0; i < n; ++i, value >>= 8)
        p[i] = (unsigned char)value;
}

static int read_footer(FILE *file, Bundle *bundle)
{
    unsigned char footer[FOOTER_SIZE];
    if (seek_file(file, 0, SEEK_END) != 0)
        return -1;
    int64_t end = tell_file(file);
    if (end < 0)
        return -1;
    bundle->offset = (uint64_t)end;
    if (end < FOOTER_SIZE)
        return 0;
    if (seek_file(file, end - FOOTER_SIZE, SEEK_SET) != 0 ||
        fread(footer, 1, sizeof(footer), file) != sizeof(footer))
        return -1;
    if (memcmp(footer, format_magic, sizeof(format_magic)) != 0)
        return 0;
    bundle->offset = read_le(footer + OFFSET_AT, 8);
    if (!bundle->offset || bundle->offset >= (uint64_t)(end - FOOTER_SIZE) ||
        !memchr(footer + ID_AT, 0, ID_SIZE))
        return -1;
    bundle->length = (uint64_t)(end - FOOTER_SIZE) - bundle->offset;
    memcpy(bundle->digest, footer + DIGEST_AT, sizeof(bundle->digest) - 1);
    bundle->digest[sizeof(bundle->digest) - 1] = '\0';
    memcpy(bundle->id, footer + ID_AT, sizeof(bundle->id));
    if (!valid_id(bundle->id) || strspn(bundle->digest, "0123456789abcdef") != sizeof(bundle->digest) - 1)
        return -1;
    return 1;
}

static bool copy_range(FILE *in, FILE *out, uint64_t size, NativeCacheHash *hash)
{
    unsigned char buffer[BUFFER_SIZE];
    while (size)
    {
        size_t chunk = size > sizeof(buffer) ? sizeof(buffer) : (size_t)size;
        if (fread(buffer, 1, chunk, in) != chunk ||
            (out && fwrite(buffer, 1, chunk, out) != chunk))
            return false;
        if (hash)
            native_cache_hash_bytes(hash, buffer, chunk);
        size -= chunk;
    }
    return true;
}

static bool write_footer(FILE *out, const Bundle *bundle)
{
    unsigned char footer[FOOTER_SIZE] = {0};
    memcpy(footer, format_magic, sizeof(format_magic));
    write_le(footer + OFFSET_AT, 8, bundle->offset);
    memcpy(footer + DIGEST_AT, bundle->digest, sizeof(bundle->digest) - 1);
    memcpy(footer + ID_AT, bundle->id, strlen(bundle->id));
    return fwrite(footer, 1, sizeof(footer), out) == sizeof(footer);
}

static bool gz_write(gzFile file, const void *data, unsigned size)
{
    return gzwrite(file, data, size) == (int)size;
}

static bool within_limits(ArchiveLimits *limits, uint64_t size)
{
    if (++limits->entries > MAX_ENTRIES || size > MAX_BYTES - limits->bytes)
        return false;
    limits->bytes += size;
    return true;
}

static bool archive_directory(gzFile archive, const char *root, const char *relative,
                              unsigned depth, ArchiveLimits *limits)
{
    char directory[PATH_SIZE];
    if (depth > MAX_DEPTH || !join(directory, root, relative))
        return false;
    DIR *dir = opendir(directory);
    if (!dir)
        return false;
    bool ok = true;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry)
        {
            ok = errno == 0
#ifdef _WIN32
                 || errno == ENOENT 
#endif
                ;
            break;
        }
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        char path[PATH_SIZE], child[PATH_SIZE];
        int length = relative[0] ? snprintf(child, sizeof(child), "%s/%s", relative, name)
                                 : snprintf(child, sizeof(child), "%s", name);
        if (!valid_component(name, strlen(name)) || !fits(length, sizeof(child)) ||
            !join(path, root, child))
        {
            fprintf(stderr, "Error: unsupported package path '%s/%s'\n", directory, name);
            ok = false;
            break;
        }
        PathKind kind = path_kind(path);
        FILE *input = kind == PATH_FILE ? fopen(path, "rb") : NULL;
        int64_t size = 0;
        if (kind != PATH_DIRECTORY)
            size = input && seek_file(input, 0, SEEK_END) == 0 ? tell_file(input) : -1;
        unsigned char header[13];
        header[0] = kind == PATH_DIRECTORY ? 'd' : 'f';
        write_le(header + 1, 4, (uint64_t)length);
        write_le(header + 5, 8, (uint64_t)size);
        ok = size >= 0 && within_limits(limits, (uint64_t)size) &&
             gz_write(archive, header, sizeof(header)) &&
             gz_write(archive, child, (unsigned)length);
        if (ok && input)
        {
            ok = seek_file(input, 0, SEEK_SET) == 0;
            unsigned char buffer[BUFFER_SIZE];
            while (ok && size)
            {
                unsigned chunk = size > BUFFER_SIZE ? BUFFER_SIZE : (unsigned)size;
                ok = fread(buffer, 1, chunk, input) == chunk &&
                     gz_write(archive, buffer, chunk);
                size -= chunk;
            }
            ok = ok && fgetc(input) == EOF && !ferror(input);
        }
        if (input)
            fclose(input);
        if (!ok)
        {
            fprintf(stderr, "Error: cannot archive '%s' (unsupported file, size limit, or I/O error)\n", path);
            break;
        }
        if (kind == PATH_DIRECTORY && !(ok = archive_directory(archive, root, child, depth + 1, limits)))
            break;
    }
    closedir(dir);
    return ok;
}

static bool extract_records(gzFile archive, const char *root)
{
    ArchiveLimits limits = {0};
    unsigned char header[13];
    while (gzread(archive, header, 1) == 1)
    {
        if (!header[0])
        {
            int status;
            int trailing = gzread(archive, header, 1);
            gzerror(archive, &status);
            return trailing == 0 && gzeof(archive) && status == Z_OK;
        }
        if ((header[0] != 'f' && header[0] != 'd') ||
            gzread(archive, header + 1, 12) != 12)
            return false;
        unsigned length = (unsigned)read_le(header + 1, 4);
        uint64_t size = read_le(header + 5, 8);
        char relative[PATH_SIZE], destination[PATH_SIZE];
        if (!length || length >= sizeof(relative) || !within_limits(&limits, size) ||
            gzread(archive, relative, length) != (int)length || memchr(relative, 0, length))
            return false;
        relative[length] = '\0';
        if (!valid_archive_path(relative) || !join(destination, root, relative))
            return false;
        if (header[0] == 'd')
        {
            if (size || !mkdir_private(destination))
                return false;
            continue;
        }
        FILE *out = fopen(destination, "wbx");
        if (!out)
            return false;
        bool ok = true;
        unsigned char buffer[BUFFER_SIZE];
        while (ok && size)
        {
            unsigned chunk = size > sizeof(buffer) ? sizeof(buffer) : (unsigned)size;
            ok = gzread(archive, buffer, chunk) == (int)chunk &&
                 fwrite(buffer, 1, chunk, out) == chunk;
            size -= chunk;
        }
        if (fclose(out) != 0 || !ok)
            return false;
    }
    return false;
}

static bool cache_root(char root[PATH_SIZE])
{
    char base[PATH_SIZE], budo[PATH_SIZE];
    const char *home;
    int length;
#ifdef _WIN32
    home = getenv("LOCALAPPDATA");
    length = home && *home ? snprintf(base, sizeof(base), "%s", home) : -1;
#elif defined(__APPLE__)
    home = getenv("HOME");
    length = home && *home ? snprintf(base, sizeof(base), "%s/Library/Application Support", home) : -1;
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    home = getenv("HOME");
    length = xdg && xdg[0] == '/' ? snprintf(base, sizeof(base), "%s", xdg)
                                 : home && *home ? snprintf(base, sizeof(base), "%s/.local/share", home) : -1;
#endif
    return fits(length, sizeof(base)) && make_base(base) && join(budo, base, "budo") &&
           make_directory(budo) && join(root, budo, "desktop-apps") && make_directory(root);
}

static bool prepare_bundle(FILE *file, const Bundle *bundle, char project[PATH_SIZE], char data[PATH_SIZE])
{
    char root[PATH_SIZE], app[PATH_SIZE], versions[PATH_SIZE], version[PATH_SIZE];
    char stage[PATH_SIZE], extracted[PATH_SIZE];
    if (!cache_root(root) || !join(app, root, bundle->id) || !make_directory(app) ||
        !join(data, app, "data") || !make_directory(data) ||
        !join(versions, app, "versions") || !make_directory(versions) ||
        !join(version, versions, bundle->digest) || !join(project, version, bundle->id))
        return false;
    PathKind version_kind = path_kind(version);
    if (version_kind != PATH_MISSING)
        return version_kind == PATH_DIRECTORY && path_kind(project) == PATH_DIRECTORY;
    if (!fs_make_temp_dir_at(versions, ".extract", stage, sizeof(stage)))
        return false;

    int fd = duplicate_file(file);
#ifdef _WIN32
    bool positioned = fd >= 0 && _lseeki64(fd, (int64_t)bundle->offset, SEEK_SET) >= 0;
#else
    bool positioned = fd >= 0 && lseek(fd, (off_t)bundle->offset, SEEK_SET) >= 0;
#endif
    gzFile archive = positioned ? gzdopen(fd, "rb") : NULL;
    bool ok = false;
    if (archive)
    {
        AppEntrypoint entry;
        char error[256];
        ok = join(extracted, stage, bundle->id) && mkdir_private(extracted) &&
             !gzdirect(archive) && extract_records(archive, extracted) &&
             app_entrypoint_resolve(extracted, ENTRYPOINTS, &entry, error, sizeof(error));
        if (gzclose(archive) != Z_OK)
            ok = false;
    }
    else if (fd >= 0)
        close_fd(fd);
    if (ok && rename(stage, version) == 0)
        return true;
    
    if (ok)
        ok = path_kind(version) == PATH_DIRECTORY && path_kind(project) == PATH_DIRECTORY;
    if (!fs_remove_tree(stage))
        fprintf(stderr, "Warning: could not remove package staging directory '%s'\n", stage);
    return ok;
}

int desktop_package_prepare(char project[PATH_SIZE], char data[PATH_SIZE])
{
    char path[PATH_SIZE];
    Bundle bundle;
    FILE *file = self_path(path) ? fopen(path, "rb") : NULL;
    int status = file ? read_footer(file, &bundle) : -1;
    if (status > 0)
    {
        NativeCacheHash hash;
        char digest[BUDO_NATIVE_CACHE_HEX_SIZE];
        native_cache_hash_init(&hash);
        native_cache_hash_component(&hash, "app-id", bundle.id, strlen(bundle.id));
        if (seek_file(file, (int64_t)bundle.offset, SEEK_SET) != 0 ||
            !copy_range(file, NULL, bundle.length, &hash))
            status = -1;
        else
        {
            native_cache_hash_finish(&hash, digest);
            if (strcmp(digest, bundle.digest) || !prepare_bundle(file, &bundle, project, data))
                status = -1;
        }
    }
    if (file)
        fclose(file);
    if (status < 0)
        fprintf(stderr, "Error: could not read or extract the embedded Budo application\n");
    return status;
}

static bool download(const char *url, const char *output)
{
#ifdef _WIN32
    char quoted[PATH_SIZE + 2];
    if (!fits(snprintf(quoted, sizeof(quoted), "\"%s\"", output), sizeof(quoted)))
        return false;
    const char *program = "curl.exe", *destination = quoted;
#else
    const char *program = "curl", *destination = output;
#endif
    const char *const args[] = {program, "-q", "--fail", "--silent", "--show-error",
                                "--location", "--proto", "=https", "--proto-redir", "=https",
                                "--output", destination, url, NULL};
#ifdef _WIN32
    return _spawnvp(_P_WAIT, program, args) == 0;
#else
    pid_t pid = fork();
    int status;
    if (pid < 0)
        return false;
    if (!pid)
    {
        execvp(program, (char *const *)args);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

static bool fetch_runtime(const char *asset, const char *tag, const char *stage, char runtime[PATH_SIZE])
{
    char base[512], url[640], checksums[PATH_SIZE], line[256];
    if (tag && (!*tag || !strcmp(tag, ".") || !strcmp(tag, "..") ||
                strspn(tag, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != strlen(tag)))
    {
        fprintf(stderr, "Error: invalid GitHub release tag '%s'\n", tag);
        return false;
    }
    int length = tag ? snprintf(base, sizeof(base), "https://github.com/" BUDO_PACKAGE_GITHUB_REPOSITORY "/releases/download/%s", tag)
                     : snprintf(base, sizeof(base), "https://github.com/" BUDO_PACKAGE_GITHUB_REPOSITORY "/releases/latest/download");
    if (!fits(length, sizeof(base)) || !join(checksums, stage, "SHA256SUMS.txt") ||
        !join(runtime, stage, asset) ||
        !fits(snprintf(url, sizeof(url), "%s/SHA256SUMS.txt", base), sizeof(url)) ||
        !download(url, checksums))
    {
        fprintf(stderr, "Error: could not download release checksums (requires curl)\n");
        return false;
    }
    FILE *file = fopen(checksums, "r");
    bool found = false;
    if (file)
    {
        while (fgets(line, sizeof(line), file))
        {
            size_t hashlen = strspn(line, "0123456789abcdef");
            if (hashlen != 64 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
                continue;
            line[strcspn(line, "\r\n")] = '\0';
            if (!strcmp(line + 66, asset))
            {
                line[64] = '\0';
                found = true;
                break;
            }
        }
        fclose(file);
    }
    if (!found)
    {
        fprintf(stderr, "Error: the release does not provide the one-file runtime '%s'\n", asset);
        return false;
    }
    char actual[BUDO_NATIVE_CACHE_HEX_SIZE];
    if (!fits(snprintf(url, sizeof(url), "%s/%s", base, asset), sizeof(url)) ||
        !download(url, runtime) || !native_cache_file_digest(runtime, actual) || strcmp(actual, line))
    {
        fprintf(stderr, "Error: could not download or verify '%s'\n", asset);
        return false;
    }
    return true;
}

static bool runtime_supports_package(FILE *file, uint64_t size)
{
    unsigned char buffer[BUFFER_SIZE + sizeof(format_magic) - 1];
    size_t overlap = 0;
    while (size)
    {
        size_t chunk = size > BUFFER_SIZE ? BUFFER_SIZE : (size_t)size;
        if (fread(buffer + overlap, 1, chunk, file) != chunk)
            return false;
        size_t count = overlap + chunk;
        for (size_t i = 0; i + sizeof(format_magic) <= count; ++i)
            if (buffer[i] == (unsigned char)format_magic[0] &&
                !memcmp(buffer + i, format_magic, sizeof(format_magic)))
                return true;
        overlap = count < sizeof(format_magic) - 1 ? count : sizeof(format_magic) - 1;
        memmove(buffer, buffer + count - overlap, overlap);
        size -= chunk;
    }
    return false;
}

static bool build_executable(const char *runtime, const char *payload, const char *output,
                             bool downloaded, Bundle *bundle)
{
    FILE *base = fopen(runtime, "rb"), *archive = fopen(payload, "rb");
    Bundle existing;
    bool ok = false;
    if (!base || !archive || read_footer(base, &existing) < 0 || !existing.offset ||
        seek_file(base, 0, SEEK_SET) != 0)
        goto done;
    if (downloaded && !runtime_supports_package(base, existing.offset))
    {
        fprintf(stderr, "Error: this Budo release does not support desktop bundles; try a newer release or 'self'\n");
        goto done;
    }
    if (seek_file(base, 0, SEEK_SET) != 0 || seek_file(archive, 0, SEEK_END) != 0)
        goto done;
    int64_t length = tell_file(archive);
    if (length <= 0 || seek_file(archive, 0, SEEK_SET) != 0)
        goto done;
    bundle->offset = existing.offset;
    NativeCacheHash hash;
    native_cache_hash_init(&hash);
    native_cache_hash_component(&hash, "app-id", bundle->id, strlen(bundle->id));
    FILE *out = fopen(output, "wbx");
    if (!out)
        goto done;
    ok = copy_range(base, out, bundle->offset, NULL) &&
         copy_range(archive, out, (uint64_t)length, &hash);
    if (ok)
    {
        native_cache_hash_finish(&hash, bundle->digest);
        ok = write_footer(out, bundle);
    }
    if (fclose(out) != 0)
        ok = false;
done:
    if (archive)
        fclose(archive);
    if (base)
        fclose(base);
    return ok;
}

static bool publish_executable(const char *runtime, const char *payload, const char *output,
                               bool downloaded, Bundle *bundle)
{
    const char *separator = last_separator(output);
    char parent[PATH_SIZE], stage[PATH_SIZE], executable[PATH_SIZE];
    if (separator && !separator[1])
        return false;
    
    if (!fits(separator ? snprintf(parent, sizeof(parent), "%.*s", (int)(separator - output + 1), output)
                        : snprintf(parent, sizeof(parent), "."), sizeof(parent)) ||
        !fs_make_temp_dir_at(parent, ".budo-package", stage, sizeof(stage)))
        return false;
    bool ok = join(executable, stage, "payload") &&
              build_executable(runtime, payload, executable, downloaded, bundle);
#ifdef _WIN32
    ok = ok && MoveFileExA(executable, output, 0);
#else
    ok = ok && chmod(executable, 0755) == 0 && link(executable, output) == 0;
#endif
    if (!fs_remove_tree(stage))
        fprintf(stderr, "Warning: could not remove package staging directory '%s'\n", stage);
    return ok;
}

int desktop_package(const char *project, const DesktopPackageOptions *options)
{
    bool self = !strcmp(options->platform, "self");
    const char *asset = !strcmp(options->platform, "linux") ? "budo-linux-x86_64" :
                        !strcmp(options->platform, "macos") ? "budo-macos-aarch64" :
                        !strcmp(options->platform, "windows") ? "budo-windows-x86_64.exe" : NULL;
    if ((!self && !asset) || (self && options->release))
    {
        fprintf(stderr, "Error: use platform self, linux, macos or windows; --release is only for downloaded runtimes\n");
        return 1;
    }
#if defined(_WIN32) && !defined(BUDO_WINDOWS_STANDALONE)
    if (self)
    {
        fprintf(stderr, "Error: this Windows build needs companion DLLs; use the standalone release or target 'windows'\n");
        return 1;
    }
#endif
    bool windows = !strcmp(options->platform, "windows");
#ifdef _WIN32
    windows = windows || self;
#endif
    Bundle bundle = {0};
    AppEntrypoint entry;
    char error[256];
    char absolute_project[PATH_SIZE];
    if (!absolute_path(project, absolute_project) || path_kind(absolute_project) != PATH_DIRECTORY)
    {
        fprintf(stderr, "Error: cannot open application directory '%s'\n", project);
        return 1;
    }
    if (!app_entrypoint_resolve(absolute_project, ENTRYPOINTS, &entry, error, sizeof(error)))
    {
        fprintf(stderr, "Error: %s\n", error);
        return 1;
    }
    if (!app_id(bundle.id, absolute_project, options->name))
    {
        fprintf(stderr, "Error: use --name with an app ID of 1-63 letters, digits, dots, underscores or dashes, starting with a letter or digit\n");
        return 1;
    }
    char default_output[ID_SIZE + 4];
    snprintf(default_output, sizeof(default_output), "%s%s", bundle.id, windows ? ".exe" : "");
    const char *output = options->output ? options->output : default_output;
    if (path_kind(output) != PATH_MISSING)
    {
        fprintf(stderr, "Error: output already exists or is inaccessible: '%s' (choose another path with -o)\n", output);
        return 1;
    }
    char stage[PATH_SIZE], payload[PATH_SIZE], runtime[PATH_SIZE];
    if (!fs_make_temp_dir("budo-package", stage, sizeof(stage)))
    {
        fprintf(stderr, "Error: could not create package staging directory: %s\n", strerror(errno));
        return 1;
    }

    char absolute_stage[PATH_SIZE];
    size_t root_length = strlen(absolute_project);
    bool ok = absolute_path(stage, absolute_stage);
    if (ok)
    {
        bool prefix;
#ifdef _WIN32
        prefix = _strnicmp(absolute_stage, absolute_project, root_length) == 0;
        bool inside = prefix && (absolute_stage[root_length] == '\\' ||
                                 absolute_project[root_length - 1] == '\\');
#else
        prefix = strncmp(absolute_stage, absolute_project, root_length) == 0;
        bool inside = prefix && (absolute_stage[root_length] == '/' ||
                                 absolute_project[root_length - 1] == '/');
#endif
        if (inside)
        {
            fprintf(stderr, "Error: the temporary directory must be outside the application directory\n");
            ok = false;
        }
    }
    ok = ok && join(payload, stage, "app.gz");
    gzFile archive = ok ? gzopen(payload, "wb") : NULL;
    if (archive)
    {
        ArchiveLimits limits = {0};
        const unsigned char end = 0;
        ok = archive_directory(archive, absolute_project, "", 1, &limits) && gz_write(archive, &end, 1);
        if (gzclose(archive) != Z_OK)
            ok = false;
    }
    else
        ok = false;
    if (ok)
        ok = self ? self_path(runtime) : fetch_runtime(asset, options->release, stage, runtime);
    if (ok)
        ok = publish_executable(runtime, payload, output, !self, &bundle);
    if (!fs_remove_tree(stage))
        fprintf(stderr, "Warning: could not remove package staging directory '%s'\n", stage);
    if (!ok)
    {
        fprintf(stderr, "Error: could not package '%s' as '%s'\n", project, output);
        return 1;
    }
    printf("Packaged %s: %s\n", bundle.id, output);
    return 0;
}