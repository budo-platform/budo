#include "file/file_wrapper.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_host_file(const char *path, const void *data, size_t length)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(length == 0 || fwrite(data, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static bool host_exists(const char *path)
{
    struct stat status;
    return lstat(path, &status) == 0;
}

int main(void)
{
    char temporary[] = "/tmp/budo-file-sandbox-XXXXXX";
    char *base = mkdtemp(temporary);
    assert(base);

    char root[FILE_MAX_PATH];
    char moved_root[FILE_MAX_PATH];
    char outside[FILE_MAX_PATH];
    char write_root[FILE_MAX_PATH];
    snprintf(root, sizeof(root), "%s/root", base);
    snprintf(moved_root, sizeof(moved_root), "%s/root-moved", base);
    snprintf(outside, sizeof(outside), "%s/outside", base);
    snprintf(write_root, sizeof(write_root), "%s/write", base);
    assert(mkdir(root, 0700) == 0);
    assert(mkdir(outside, 0700) == 0);
    assert(mkdir(write_root, 0700) == 0);

    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/secret.bin", outside);
    write_host_file(path, "secret", 6);
    snprintf(path, sizeof(path), "%s/inside.bin", root);
    write_host_file(path, "inside", 6);
    snprintf(path, sizeof(path), "%s/empty.bin", root);
    write_host_file(path, NULL, 0);

    char link_path[FILE_MAX_PATH];
    snprintf(link_path, sizeof(link_path), "%s/file-link", root);
    assert(symlink("../outside/secret.bin", link_path) == 0);
    snprintf(link_path, sizeof(link_path), "%s/dir-link", root);
    assert(symlink("../outside", link_path) == 0);
    snprintf(link_path, sizeof(link_path), "%s/file-link", write_root);
    assert(symlink("../outside/secret.bin", link_path) == 0);
    snprintf(link_path, sizeof(link_path), "%s/dir-link", write_root);
    assert(symlink("../outside", link_path) == 0);

    FileContext *ctx = file_create(root);
    assert(ctx && file_has_root(ctx));
    file_set_write_root(ctx, write_root);
    assert(file_has_write_root(ctx));

    FileListResult *virtual_listing = file_list(ctx, "");
    assert(virtual_listing && virtual_listing->count == 2);
    assert(strcmp(virtual_listing->entries[0].name, "assets") == 0);
    assert(strcmp(virtual_listing->entries[1].name, "files") == 0);
    file_list_free(virtual_listing);
    assert(file_is_directory(ctx, "assets"));
    assert(file_is_directory(ctx, "files"));

    size_t length = 123;
    uint8_t *bytes = file_read_binary(ctx, "assets/empty.bin", &length);
    assert(bytes != NULL);
    assert(length == 0);
    free(bytes);

    assert(file_read_binary(ctx, "assets/file-link", &length) == NULL);
    FileNativeReference native_ref;
    assert(!file_native_open(ctx, "assets/file-link", &native_ref));
    assert(!file_native_open(ctx, "assets/dir-link/secret.bin", &native_ref));
    assert(file_native_open(ctx, "assets/inside.bin", &native_ref));
    assert(native_ref.size == 6);
#ifndef _WIN32
    assert(native_ref.fd >= 0);
#endif
    assert(native_ref.identity_high != 0 || native_ref.identity_low != 0);
    file_native_close(&native_ref);
#ifndef _WIN32
    assert(native_ref.fd == -1);
#endif
    assert(!file_is_file(ctx, "assets/file-link"));
    assert(file_size(ctx, "assets/file-link") == -1);
    assert(!file_write_binary(ctx, "files/file-link", (const uint8_t *)"changed", 7));
    snprintf(path, sizeof(path), "%s/secret.bin", outside);
    FILE *secret = fopen(path, "rb");
    assert(secret);
    char secret_contents[7] = {0};
    assert(fread(secret_contents, 1, 6, secret) == 6);
    assert(fclose(secret) == 0);
    assert(strcmp(secret_contents, "secret") == 0);
    assert(file_read_binary(ctx, "assets/dir-link/secret.bin", &length) == NULL);
    assert(file_read_binary(ctx, "assets/dir-link/missing.bin", &length) == NULL);
    assert(!file_write_binary(ctx, "files/dir-link/created.bin", (const uint8_t *)"x", 1));
    snprintf(path, sizeof(path), "%s/created.bin", outside);
    assert(!host_exists(path));

    assert(file_read_binary(ctx, "assets/../outside/secret.bin", &length) == NULL);
    assert(!file_write_text(ctx, "files/nested/../../escape.txt", "x", 1));
    assert(!file_write_text(ctx, "assets/no.txt", "x", 1));
    assert(!file_write_text(ctx, "unmounted/no.txt", "x", 1));

    FileListResult *listing = file_list(ctx, "assets");
    assert(listing);
    bool saw_directory_symlink = false;
    for (int i = 0; i < listing->count; i++)
    {
        if (strcmp(listing->entries[i].name, "dir-link") == 0)
        {
            saw_directory_symlink = true;
            assert(listing->entries[i].type == FILE_ENTRY_FILE);
        }
    }
    assert(saw_directory_symlink);
    file_list_free(listing);

    snprintf(path, sizeof(path), "%s/files-only.txt", write_root);
    write_host_file(path, "files", 5);
    assert(file_is_file_at(ctx, FILE_ROOT_FILES, "files-only.txt"));
    assert(!file_is_file_at(ctx, FILE_ROOT_ASSETS, "files-only.txt"));

    size_t files_length = 0;
    char *files_text = file_read_text_at(ctx, FILE_ROOT_FILES, "files-only.txt",
                                         &files_length);
    assert(files_text && files_length == 5 && strcmp(files_text, "files") == 0);
    free(files_text);
    assert(file_write_binary(ctx, "files/one/two/value.bin", (const uint8_t *)"value", 5));
    assert(file_append_binary(ctx, "files/one/two/value.bin", (const uint8_t *)"-more", 5));
    snprintf(path, sizeof(path), "%s/one/two/value.bin", write_root);
    assert(host_exists(path));
    assert(strstr(file_get_last_write_path(ctx), "/one/two/value.bin") != NULL);
    bytes = file_read_binary(ctx, "files/one/two/value.bin", &length);
    assert(bytes && length == 10 && memcmp(bytes, "value-more", 10) == 0);
    free(bytes);
    bytes = file_read_binary(ctx, "assets/inside.bin", &length);
    assert(bytes && length == 6 && memcmp(bytes, "inside", 6) == 0);
    free(bytes);

    assert(rename(root, moved_root) == 0);
    assert(symlink(outside, root) == 0);
    bytes = file_read_binary(ctx, "assets/inside.bin", &length);
    assert(bytes && length == 6 && memcmp(bytes, "inside", 6) == 0);
    free(bytes);
    assert(file_read_binary(ctx, "assets/secret.bin", &length) == NULL);

    file_set_root(ctx, outside);
    bytes = file_read_binary(ctx, "assets/secret.bin", &length);
    assert(bytes && length == 6 && memcmp(bytes, "secret", 6) == 0);
    free(bytes);

    file_destroy(ctx);

    unlink(root);
    snprintf(path, sizeof(path), "%s/file-link", moved_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/dir-link", moved_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/inside.bin", moved_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/empty.bin", moved_root);
    unlink(path);
    rmdir(moved_root);
    snprintf(path, sizeof(path), "%s/secret.bin", outside);
    unlink(path);
    rmdir(outside);
    snprintf(path, sizeof(path), "%s/one/two/value.bin", write_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/file-link", write_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/dir-link", write_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/one/two", write_root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/one", write_root);
    rmdir(path);
    rmdir(write_root);
    rmdir(base);

    puts("file sandbox tests passed");
    return 0;
}