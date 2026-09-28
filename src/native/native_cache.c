#include "native_cache.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#define chmod _chmod
#define lstat stat
#define rmdir _rmdir
#define unlink _unlink
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

static uint32_t rotate_right(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void sha256_block(NativeCacheHash *hash, const unsigned char *block)
{
    static const uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t words[64], a, b, c, d, e, f, g, h;
    size_t index;
    for (index = 0; index < 16; index++)
        words[index] = ((uint32_t)block[index * 4] << 24) |
                       ((uint32_t)block[index * 4 + 1] << 16) |
                       ((uint32_t)block[index * 4 + 2] << 8) |
                       block[index * 4 + 3];
    for (; index < 64; index++)
    {
        uint32_t s0 = rotate_right(words[index - 15], 7) ^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3);
        uint32_t s1 = rotate_right(words[index - 2], 17) ^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    a = hash->state[0];
    b = hash->state[1];
    c = hash->state[2];
    d = hash->state[3];
    e = hash->state[4];
    f = hash->state[5];
    g = hash->state[6];
    h = hash->state[7];
    for (index = 0; index < 64; index++)
    {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choice + constants[index] + words[index];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    hash->state[0] += a;
    hash->state[1] += b;
    hash->state[2] += c;
    hash->state[3] += d;
    hash->state[4] += e;
    hash->state[5] += f;
    hash->state[6] += g;
    hash->state[7] += h;
}

void native_cache_hash_init(NativeCacheHash *hash)
{
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(hash->state, initial, sizeof(initial));
    hash->bit_count = 0;
    hash->block_size = 0;
}

void native_cache_hash_bytes(NativeCacheHash *hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    hash->bit_count += (uint64_t)size * 8;
    while (size)
    {
        size_t amount = 64 - hash->block_size;
        if (amount > size)
            amount = size;
        memcpy(hash->block + hash->block_size, bytes, amount);
        hash->block_size += amount;
        bytes += amount;
        size -= amount;
        if (hash->block_size == 64)
        {
            sha256_block(hash, hash->block);
            hash->block_size = 0;
        }
    }
}

static void hash_u64(NativeCacheHash *hash, uint64_t value)
{
    unsigned char bytes[8];
    int index;
    for (index = 7; index >= 0; index--)
    {
        bytes[index] = (unsigned char)value;
        value >>= 8;
    }
    native_cache_hash_bytes(hash, bytes, sizeof(bytes));
}

void native_cache_hash_component(NativeCacheHash *hash, const char *name,
                                 const void *data, size_t size)
{
    size_t name_length = strlen(name);
    hash_u64(hash, name_length);
    native_cache_hash_bytes(hash, name, name_length);
    hash_u64(hash, size);
    native_cache_hash_bytes(hash, data, size);
}

bool native_cache_hash_file_component(NativeCacheHash *hash, const char *name,
                                      const char *logical_path, const char *file_path)
{
    FILE *file = fopen(file_path, "rb");
    unsigned char buffer[16384];
    size_t amount;
    struct stat info;
    if (!file || stat(file_path, &info) != 0)
    {
        if (file)
            fclose(file);
        return false;
    }
    native_cache_hash_component(hash, name, logical_path, strlen(logical_path));
    hash_u64(hash, (uint64_t)info.st_size);
    while ((amount = fread(buffer, 1, sizeof(buffer), file)) != 0)
        native_cache_hash_bytes(hash, buffer, amount);
    if (ferror(file))
    {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static int compare_names(const void *left, const void *right)
{
    const char *const *a = left, *const *b = right;
    return strcmp(*a, *b);
}

static bool hash_tree(NativeCacheHash *hash, const char *name, const char *logical,
                      const char *directory)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    char **names = NULL;
    size_t count = 0, capacity = 0, index;
    bool ok = true;
    if (!dir)
        return false;
    while ((entry = readdir(dir)) != NULL)
    {
        char *copy;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (count == capacity)
        {
            size_t next = capacity ? capacity * 2 : 16;
            char **grown = realloc(names, next * sizeof(*names));
            if (!grown)
            {
                ok = false;
                break;
            }
            names = grown;
            capacity = next;
        }
        copy = malloc(strlen(entry->d_name) + 1);
        if (!copy)
        {
            ok = false;
            break;
        }
        strcpy(copy, entry->d_name);
        names[count++] = copy;
    }
    closedir(dir);
    qsort(names, count, sizeof(*names), compare_names);
    for (index = 0; ok && index < count; index++)
    {
        char physical[BUDO_NATIVE_CACHE_PATH_SIZE], child_logical[BUDO_NATIVE_CACHE_PATH_SIZE];
        struct stat info;
        if (snprintf(physical, sizeof(physical), "%s/%s", directory, names[index]) >= (int)sizeof(physical) ||
            snprintf(child_logical, sizeof(child_logical), "%s/%s", logical, names[index]) >= (int)sizeof(child_logical) ||
            lstat(physical, &info) != 0)
        {
            ok = false;
            break;
        }
        if (S_ISDIR(info.st_mode))
            ok = hash_tree(hash, name, child_logical, physical);
        else if (S_ISREG(info.st_mode))
            ok = native_cache_hash_file_component(hash, name, child_logical, physical);
    }
    for (index = 0; index < count; index++)
        free(names[index]);
    free(names);
    return ok;
}

bool native_cache_hash_tree_component(NativeCacheHash *hash, const char *name,
                                      const char *logical_root, const char *directory)
{
    return hash_tree(hash, name, logical_root, directory);
}

static bool is_header_path(const char *path)
{
    const char *extension = strrchr(path, '.');
    return extension &&
           (!strcmp(extension, ".h") || !strcmp(extension, ".hh") ||
            !strcmp(extension, ".hpp") || !strcmp(extension, ".hxx") ||
            !strcmp(extension, ".inc") || !strcmp(extension, ".inl"));
}

static bool hash_header_tree(NativeCacheHash *hash, const char *logical,
                             const char *directory)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    char **names = NULL;
    size_t count = 0, capacity = 0, index;
    bool ok = dir != NULL;
    if (!dir)
        return false;
    while (ok && (entry = readdir(dir)) != NULL)
    {
        char *copy;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !strcmp(entry->d_name, ".budo"))
            continue;
        if (count == capacity)
        {
            size_t next = capacity ? capacity * 2 : 16;
            char **grown = realloc(names, next * sizeof(*names));
            if (!grown)
            {
                ok = false;
                break;
            }
            names = grown;
            capacity = next;
        }
        copy = malloc(strlen(entry->d_name) + 1);
        if (!copy)
        {
            ok = false;
            break;
        }
        strcpy(copy, entry->d_name);
        names[count++] = copy;
    }
    closedir(dir);
    qsort(names, count, sizeof(*names), compare_names);
    for (index = 0; ok && index < count; index++)
    {
        char physical[BUDO_NATIVE_CACHE_PATH_SIZE];
        char child_logical[BUDO_NATIVE_CACHE_PATH_SIZE];
        struct stat info;
        if (snprintf(physical, sizeof(physical), "%s/%s", directory,
                     names[index]) >= (int)sizeof(physical) ||
            snprintf(child_logical, sizeof(child_logical), "%s/%s", logical,
                     names[index]) >= (int)sizeof(child_logical) ||
            lstat(physical, &info) != 0)
            ok = false;
        else if (S_ISDIR(info.st_mode))
            ok = hash_header_tree(hash, child_logical, physical);
        else if (S_ISREG(info.st_mode) && is_header_path(names[index]))
            ok = native_cache_hash_file_component(hash, "project-header",
                                                  child_logical, physical);
    }
    for (index = 0; index < count; index++)
        free(names[index]);
    free(names);
    return ok;
}

bool native_cache_hash_header_tree_component(NativeCacheHash *hash,
                                             const char *logical_root,
                                             const char *directory)
{
    return hash_header_tree(hash, logical_root, directory);
}

void native_cache_hash_finish(NativeCacheHash *hash, char output[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    uint64_t bits = hash->bit_count;
    size_t index;
    unsigned char one = 0x80, zero = 0;
    native_cache_hash_bytes(hash, &one, 1);
    while (hash->block_size != 56)
        native_cache_hash_bytes(hash, &zero, 1);
    {
        unsigned char length[8];
        for (index = 0; index < 8; index++)
            length[7 - index] = (unsigned char)(bits >> (index * 8));
        native_cache_hash_bytes(hash, length, 8);
    }
    for (index = 0; index < 8; index++)
    {
        digest[index * 4] = (unsigned char)(hash->state[index] >> 24);
        digest[index * 4 + 1] = (unsigned char)(hash->state[index] >> 16);
        digest[index * 4 + 2] = (unsigned char)(hash->state[index] >> 8);
        digest[index * 4 + 3] = (unsigned char)hash->state[index];
    }
    for (index = 0; index < 32; index++)
    {
        output[index * 2] = hex[digest[index] >> 4];
        output[index * 2 + 1] = hex[digest[index] & 15];
    }
    output[64] = '\0';
}

bool native_cache_file_digest(const char *path, char output[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    NativeCacheHash hash;
    FILE *file = fopen(path, "rb");
    unsigned char buffer[16384];
    size_t amount;
    if (!file)
        return false;
    native_cache_hash_init(&hash);
    while ((amount = fread(buffer, 1, sizeof(buffer), file)) != 0)
        native_cache_hash_bytes(&hash, buffer, amount);
    if (ferror(file))
    {
        fclose(file);
        return false;
    }
    fclose(file);
    native_cache_hash_finish(&hash, output);
    return true;
}

bool native_cache_make_directories(const char *path)
{
    char copy[BUDO_NATIVE_CACHE_PATH_SIZE];
    char *cursor;
    if (!path || strlen(path) >= sizeof(copy))
        return false;
    strcpy(copy, path);
    for (cursor = copy + 1; *cursor; cursor++)
        if (*cursor == '/' || *cursor == '\\')
        {
            char saved = *cursor;
            *cursor = '\0';
            if (mkdir(copy, 0755) != 0 && errno != EEXIST)
                return false;
            *cursor = saved;
        }
    return mkdir(copy, 0755) == 0 || errno == EEXIST;
}

bool native_cache_root(char *path, size_t size)
{
    const char *override = getenv("BUDO_NATIVE_CACHE_DIR");
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
        length = home ? snprintf(path, size, "%s/Budo/native-builds", home) : -1;
    }
#else
    else
    {
        const char *xdg = getenv("XDG_CACHE_HOME");
        home = getenv("HOME");
        length = xdg && xdg[0] ? snprintf(path, size, "%s/budo/native-builds", xdg) : (home ? snprintf(path, size, "%s/.cache/budo/native-builds", home) : -1);
    }
#endif
    return length >= 0 && (size_t)length < size;
}

bool native_cache_lock_acquire(const char *path, NativeCacheLock *lock)
{
    char parent[BUDO_NATIVE_CACHE_PATH_SIZE];
    char *slash;
    if (strlen(path) >= sizeof(parent))
        return false;
    strcpy(parent, path);
    slash = strrchr(parent, '/');
    if (slash)
    {
        *slash = '\0';
        if (!native_cache_make_directories(parent))
            return false;
    }
#ifdef _WIN32
    lock->handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (lock->handle == INVALID_HANDLE_VALUE)
        return false;
    {
        OVERLAPPED overlap = {0};
        if (!LockFileEx(lock->handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap))
        {
            CloseHandle(lock->handle);
            return false;
        }
    }
#else
    lock->descriptor = open(path, O_CREAT | O_RDWR, 0644);
    if (lock->descriptor < 0 || flock(lock->descriptor, LOCK_EX) != 0)
    {
        if (lock->descriptor >= 0)
            close(lock->descriptor);
        return false;
    }
#endif
    return true;
}

void native_cache_lock_release(NativeCacheLock *lock)
{
#ifdef _WIN32
    if (lock->handle && lock->handle != INVALID_HANDLE_VALUE)
    {
        OVERLAPPED overlap = {0};
        UnlockFileEx(lock->handle, 0, 1, 0, &overlap);
        CloseHandle(lock->handle);
        lock->handle = NULL;
    }
#else
    if (lock->descriptor >= 0)
    {
        flock(lock->descriptor, LOCK_UN);
        close(lock->descriptor);
        lock->descriptor = -1;
    }
#endif
}

static bool copy_atomic(const char *source, const char *destination)
{
    char temporary[BUDO_NATIVE_CACHE_PATH_SIZE];
    FILE *in, *out;
    unsigned char buffer[16384];
    size_t amount;
    struct stat info;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%lu", destination, (unsigned long)time(NULL)) >= (int)sizeof(temporary))
        return false;
    in = fopen(source, "rb");
    if (!in)
        return false;
    out = fopen(temporary, "wb");
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
            unlink(temporary);
            return false;
        }
    if (ferror(in) || fclose(in) != 0 || fclose(out) != 0)
    {
        unlink(temporary);
        return false;
    }
    if (stat(source, &info) == 0)
        chmod(temporary, info.st_mode);
#ifdef _WIN32
    unlink(destination);
#endif
    if (rename(temporary, destination) != 0)
    {
        unlink(temporary);
        return false;
    }
    return true;
}

static bool read_digest(const char *path, char digest[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    FILE *file = fopen(path, "rb");
    size_t length;
    if (!file)
        return false;
    length = fread(digest, 1, 64, file);
    fclose(file);
    digest[length] = '\0';
    return length == 64;
}

int native_cache_lookup(const char *key, const char *destination, char output_digest[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    char root[BUDO_NATIVE_CACHE_PATH_SIZE], entry[BUDO_NATIVE_CACHE_PATH_SIZE], executable[BUDO_NATIVE_CACHE_PATH_SIZE], digest_path[BUDO_NATIVE_CACHE_PATH_SIZE], complete[BUDO_NATIVE_CACHE_PATH_SIZE], actual[65], parent[BUDO_NATIVE_CACHE_PATH_SIZE], lock_path[BUDO_NATIVE_CACHE_PATH_SIZE], *slash;
    NativeCacheLock lock;
    struct stat info;
    int result = 0;
    if (!native_cache_root(root, sizeof(root)) || snprintf(entry, sizeof(entry), "%s/sha256/%s", root, key) >= (int)sizeof(entry))
        return 0;
    if (snprintf(lock_path, sizeof(lock_path), "%s/locks/%s.lock", root, key) >=
            (int)sizeof(lock_path) ||
        !native_cache_lock_acquire(lock_path, &lock))
        return -1;
    snprintf(executable, sizeof(executable), "%s/executable", entry);
    snprintf(digest_path, sizeof(digest_path), "%s/output.sha256", entry);
    snprintf(complete, sizeof(complete), "%s/complete", entry);
    if (stat(complete, &info) != 0)
        goto done;
    if (!read_digest(digest_path, output_digest) || !native_cache_file_digest(executable, actual) || strcmp(actual, output_digest) != 0)
    {
        result = -1;
        goto done;
    }
    if (strlen(destination) >= sizeof(parent))
    {
        result = -1;
        goto done;
    }
    strcpy(parent, destination);
    slash = strrchr(parent, '/');
    if (slash)
    {
        *slash = '\0';
        if (!native_cache_make_directories(parent))
        {
            result = -1;
            goto done;
        }
    }
    result = copy_atomic(executable, destination) ? 1 : -1;
done:
    native_cache_lock_release(&lock);
    return result;
}

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    if (fputs(text, file) < 0)
    {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

bool native_cache_publish(const char *key, const char *executable, const char *output_digest, const char *record_json)
{
    char root[1024], base[1024], entry[1024], temporary[1024], path[1024];
    NativeCacheLock lock;
    bool ok = false;
    struct stat info;
    if (!native_cache_root(root, sizeof(root)) || snprintf(base, sizeof(base), "%s/sha256", root) >= (int)sizeof(base) || !native_cache_make_directories(base))
        return false;
    snprintf(path, sizeof(path), "%s/locks/%s.lock", root, key);
    if (!native_cache_lock_acquire(path, &lock))
        return false;
    snprintf(entry, sizeof(entry), "%s/%s", base, key);
    snprintf(path, sizeof(path), "%s/complete", entry);
    if (stat(path, &info) == 0)
    {
        native_cache_lock_release(&lock);
        return true;
    }
    snprintf(temporary, sizeof(temporary), "%s/.%s.tmp.%lu", base, key, (unsigned long)time(NULL));
    if (mkdir(temporary, 0755) != 0)
        goto done;
    snprintf(path, sizeof(path), "%s/executable", temporary);
    if (!copy_atomic(executable, path))
        goto done;
    snprintf(path, sizeof(path), "%s/output.sha256", temporary);
    {
        char line[80];
        snprintf(line, sizeof(line), "%s\n", output_digest);
        if (!write_text(path, line))
            goto done;
    }
    snprintf(path, sizeof(path), "%s/record.json", temporary);
    if (!write_text(path, record_json))
        goto done;
    snprintf(path, sizeof(path), "%s/complete", temporary);
    if (!write_text(path, "complete\n"))
        goto done;
    if (rename(temporary, entry) != 0 && errno != EEXIST)
        goto done;
    ok = true;
done:
    native_cache_lock_release(&lock);
    return ok;
}

static uint64_t directory_size(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    uint64_t size = 0;
    if (!dir)
        return 0;
    while ((entry = readdir(dir)))
    {
        char child[1024];
        struct stat info;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (lstat(child, &info) == 0)
            size += S_ISDIR(info.st_mode) ? directory_size(child) : (uint64_t)info.st_size;
    }
    closedir(dir);
    return size;
}

static bool remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    bool ok = true;
    if (!dir)
        return errno == ENOENT;
    while ((entry = readdir(dir)))
    {
        char child[1024];
        struct stat info;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (lstat(child, &info) != 0)
            ok = false;
        else if (S_ISDIR(info.st_mode))
        {
            if (!remove_tree(child))
                ok = false;
        }
        else if (unlink(child) != 0)
            ok = false;
    }
    closedir(dir);
    return rmdir(path) == 0 && ok;
}

bool native_cache_remove(const char *key)
{
    char root[1024], path[1024], lock_path[1024];
    NativeCacheLock lock;
    bool result;
    if (!native_cache_root(root, sizeof(root)) ||
        snprintf(path, sizeof(path), "%s/sha256/%s", root, key) >= (int)sizeof(path) ||
        snprintf(lock_path, sizeof(lock_path), "%s/locks/%s.lock", root, key) >=
            (int)sizeof(lock_path) ||
        !native_cache_lock_acquire(lock_path, &lock))
        return false;
    result = remove_tree(path);
    native_cache_lock_release(&lock);
    return result;
}

int native_cache_inspect(bool json)
{
    char root[1024], base[1024];
    DIR *dir;
    struct dirent *entry;
    size_t count = 0;
    uint64_t total = 0;
    if (!native_cache_root(root, sizeof(root)) || snprintf(base, sizeof(base), "%s/sha256", root) >= (int)sizeof(base))
        return 1;
    dir = opendir(base);
    if (json)
        printf("{\"root\":\"%s\",\"entries\":[", root);
    else
        printf("Native build cache: %s\n", root);
    if (dir)
        while ((entry = readdir(dir)))
        {
            char path[1024], complete[1024];
            struct stat info;
            uint64_t size;
            if (entry->d_name[0] == '.')
                continue;
            snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
            snprintf(complete, sizeof(complete), "%s/complete", path);
            if (stat(complete, &info) != 0)
                continue;
            size = directory_size(path);
            if (json)
                printf("%s{\"key\":\"%s\",\"bytes\":%llu}", count ? "," : "", entry->d_name, (unsigned long long)size);
            else
                printf("  %s  %llu bytes\n", entry->d_name, (unsigned long long)size);
            count++;
            total += size;
        }
    if (dir)
        closedir(dir);
    if (json)
        printf("],\"count\":%zu,\"bytes\":%llu}\n", count, (unsigned long long)total);
    else
        printf("%zu entries, %llu bytes\n", count, (unsigned long long)total);
    return 0;
}

int native_cache_clean_all(void)
{
    char root[1024], base[1024];
    DIR *dir;
    struct dirent *entry;
    size_t removed = 0;
    if (!native_cache_root(root, sizeof(root)) || snprintf(base, sizeof(base), "%s/sha256", root) >= (int)sizeof(base))
        return 1;
    dir = opendir(base);
    if (!dir)
    {
        printf("Native build cache is empty.\n");
        return 0;
    }
    while ((entry = readdir(dir)))
    {
        if (entry->d_name[0] == '.')
            continue;
        if (native_cache_remove(entry->d_name))
            removed++;
    }
    closedir(dir);
    printf("Removed %zu native build cache entries.\n", removed);
    return 0;
}