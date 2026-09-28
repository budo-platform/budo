#include "file/file_wrapper.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_directory(const char *path)
{
    assert(CreateDirectoryA(path, NULL));
}

static void write_host_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    size_t length = strlen(contents);
    assert(fwrite(contents, 1, length, file) == length);
    assert(fclose(file) == 0);
}

int main(void)
{
    char temp_path[MAX_PATH];
    char base[MAX_PATH];
    assert(GetTempPathA(MAX_PATH, temp_path) > 0);
    assert(GetTempFileNameA(temp_path, "bfs", 0, base) != 0);
    assert(DeleteFileA(base));
    make_directory(base);

    char root[MAX_PATH];
    char outside[MAX_PATH];
    char write_root[MAX_PATH];
    snprintf(root, sizeof(root), "%s\\root", base);
    snprintf(outside, sizeof(outside), "%s\\outside", base);
    snprintf(write_root, sizeof(write_root), "%s\\write", base);
    make_directory(root);
    make_directory(outside);
    make_directory(write_root);

    char inside[MAX_PATH];
    char secret[MAX_PATH];
    snprintf(inside, sizeof(inside), "%s\\inside.bin", root);
    snprintf(secret, sizeof(secret), "%s\\secret.bin", outside);
    write_host_file(inside, "inside");
    write_host_file(secret, "secret");

    FileContext *ctx = file_create(root);
    assert(ctx && file_has_root(ctx));
    size_t length = 0;
    uint8_t *bytes = file_read_binary(ctx, "assets/inside.bin", &length);
    assert(bytes && length == 6 && memcmp(bytes, "inside", 6) == 0);
    free(bytes);
    assert(file_size(ctx, "assets/inside.bin") == 6);

    file_set_write_root(ctx, write_root);
    assert(file_has_write_root(ctx));
    assert(file_write_text(ctx, "files/one/two/value.txt", "value", 5));
    assert(strstr(file_get_last_write_path(ctx), "one/two/value.txt") != NULL);

    FileListResult *listing = file_list(ctx, "");
    assert(listing && listing->count == 2);
    assert(strcmp(listing->entries[0].name, "assets") == 0);
    assert(strcmp(listing->entries[1].name, "files") == 0);
    file_list_free(listing);
    listing = file_list(ctx, "assets");
    assert(listing && listing->count == 1);
    assert(strcmp(listing->entries[0].name, "inside.bin") == 0);
    file_list_free(listing);

    char file_link[MAX_PATH];
    char directory_link[MAX_PATH];
    char write_file_link[MAX_PATH];
    char write_directory_link[MAX_PATH];
    snprintf(file_link, sizeof(file_link), "%s\\file-link", root);
    snprintf(directory_link, sizeof(directory_link), "%s\\dir-link", root);
    snprintf(write_file_link, sizeof(write_file_link), "%s\\file-link", write_root);
    snprintf(write_directory_link, sizeof(write_directory_link), "%s\\dir-link", write_root);
    DWORD link_flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    link_flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
    BOOL file_link_created = CreateSymbolicLinkA(file_link, secret, link_flags);
    BOOL directory_link_created = CreateSymbolicLinkA(
        directory_link, outside, link_flags | SYMBOLIC_LINK_FLAG_DIRECTORY);
    BOOL write_file_link_created = CreateSymbolicLinkA(write_file_link, secret, link_flags);
    BOOL write_directory_link_created = CreateSymbolicLinkA(
        write_directory_link, outside, link_flags | SYMBOLIC_LINK_FLAG_DIRECTORY);
    if (file_link_created)
        assert(file_read_binary(ctx, "assets/file-link", &length) == NULL);
    if (directory_link_created)
        assert(file_read_binary(ctx, "assets/dir-link/secret.bin", &length) == NULL);
    if (write_file_link_created)
        assert(!file_write_text(ctx, "files/file-link", "changed", 7));
    if (write_directory_link_created)
        assert(!file_write_text(ctx, "files/dir-link/escape.bin", "x", 1));
    assert(file_read_binary(ctx, "assets/../outside/secret.bin", &length) == NULL);
    assert(file_read_binary(ctx, "\\outside\\secret.bin", &length) == NULL);
    assert(file_read_binary(ctx, "C:/Windows/win.ini", &length) == NULL);

    file_destroy(ctx);
    if (file_link_created)
        DeleteFileA(file_link);
    if (directory_link_created)
        RemoveDirectoryA(directory_link);
    if (write_file_link_created)
        DeleteFileA(write_file_link);
    if (write_directory_link_created)
        RemoveDirectoryA(write_directory_link);
    DeleteFileA(inside);
    DeleteFileA(secret);
    char value[MAX_PATH];
    snprintf(value, sizeof(value), "%s\\one\\two\\value.txt", write_root);
    DeleteFileA(value);
    snprintf(value, sizeof(value), "%s\\one\\two", write_root);
    RemoveDirectoryA(value);
    snprintf(value, sizeof(value), "%s\\one", write_root);
    RemoveDirectoryA(value);
    RemoveDirectoryA(write_root);
    RemoveDirectoryA(root);
    RemoveDirectoryA(outside);
    RemoveDirectoryA(base);
    puts("Windows file sandbox tests passed");
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif