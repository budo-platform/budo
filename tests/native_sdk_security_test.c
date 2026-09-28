#include "native/native_sdk.h"
#include "native/native_build_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

typedef struct TarWriter
{
    gzFile file;
} TarWriter;

static void octal(unsigned char *field, size_t size, unsigned long long value)
{
    char format[32];
    snprintf(format, sizeof(format), "%%0%zullo", size - 1);
    snprintf((char *)field, size, format, value);
}

static void add_member(TarWriter *writer, const char *name, char type,
                       const void *contents, size_t size, unsigned long long claimed)
{
    unsigned char header[512] = {0}, padding[512] = {0};
    unsigned checksum = 0;
    size_t index;
    assert(strlen(name) < 100);
    memcpy(header, name, strlen(name));
    octal(header + 100, 8, type == '5' ? 0755 : 0644);
    octal(header + 108, 8, 0);
    octal(header + 116, 8, 0);
    octal(header + 124, 12, claimed);
    octal(header + 136, 12, 0);
    memset(header + 148, ' ', 8);
    header[156] = (unsigned char)type;
    memcpy(header + 257, "ustar", 5);
    memcpy(header + 263, "00", 2);
    for (index = 0; index < sizeof(header); index++)
        checksum += header[index];
    snprintf((char *)header + 148, 8, "%06o", checksum);
    header[154] = '\0';
    header[155] = ' ';
    assert(gzwrite(writer->file, header, sizeof(header)) == sizeof(header));
    if (size)
        assert(gzwrite(writer->file, contents, (unsigned)size) == (int)size);
    if (size % 512)
        assert(gzwrite(writer->file, padding, (unsigned)(512 - size % 512)) ==
               (int)(512 - size % 512));
}

static void finish(TarWriter *writer)
{
    unsigned char zeros[1024] = {0};
    assert(gzwrite(writer->file, zeros, sizeof(zeros)) == sizeof(zeros));
    assert(gzclose(writer->file) == Z_OK);
}

static void expect_rejected(const char *root, const char *label, const char *name,
                            char type, unsigned long long claimed)
{
    char archive[1024], output[1024], error[512] = "";
    TarWriter writer;
    snprintf(archive, sizeof(archive), "%s/%s.tar.gz", root, label);
    snprintf(output, sizeof(output), "%s/%s-out", root, label);
    writer.file = gzopen(archive, "wb9");
    assert(writer.file);
    add_member(&writer, name, type, NULL, 0, claimed);
    finish(&writer);
    assert(!native_sdk_extract_tar_gz(archive, output, error, sizeof(error)));
    assert(error[0]);
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

int main(void)
{
    char root[1024], archive[1024], output[1024], path[1024], manifest[1024];
    char digest[65], error[512] = "";
    NativeSdkSelection selection = {0};
    TarWriter writer;
    const char payload[] = "safe\n";
#ifdef _WIN32
    snprintf(root, sizeof(root), "native-sdk-security-%lu", (unsigned long)GetCurrentProcessId());
#else
    snprintf(root, sizeof(root), "/tmp/native-sdk-security-%lu", (unsigned long)getpid());
#endif
    mkdir(root, 0755);

    expect_rejected(root, "absolute", "/absolute", '0', 0);
    expect_rejected(root, "traversal", "top/../escape", '0', 0);
    expect_rejected(root, "symlink", "top/link", '2', 0);
    expect_rejected(root, "hardlink", "top/hard", '1', 0);
    expect_rejected(root, "special", "top/fifo", '6', 0);
    expect_rejected(root, "oversize", "top/huge", '0',
                    512ULL * 1024ULL * 1024ULL + 1ULL);

    snprintf(archive, sizeof(archive), "%s/duplicate.tar.gz", root);
    snprintf(output, sizeof(output), "%s/duplicate-out", root);
    writer.file = gzopen(archive, "wb9");
    add_member(&writer, "top/file", '0', payload, sizeof(payload) - 1,
               sizeof(payload) - 1);
    add_member(&writer, "top/file", '0', payload, sizeof(payload) - 1,
               sizeof(payload) - 1);
    finish(&writer);
    assert(!native_sdk_extract_tar_gz(archive, output, error, sizeof(error)));

    snprintf(archive, sizeof(archive), "%s/member-bomb.tar.gz", root);
    snprintf(output, sizeof(output), "%s/member-bomb-out", root);
    writer.file = gzopen(archive, "wb1");
    for (size_t index = 0; index <= 10000; index++)
    {
        char name[64];
        snprintf(name, sizeof(name), "top/d%05zu/", index);
        add_member(&writer, name, '5', NULL, 0, 0);
    }
    finish(&writer);
    assert(!native_sdk_extract_tar_gz(archive, output, error, sizeof(error)));

    snprintf(archive, sizeof(archive), "%s/safe.tar.gz", root);
    snprintf(output, sizeof(output), "%s/safe-out", root);
    writer.file = gzopen(archive, "wb9");
    add_member(&writer, "top/share/", '5', NULL, 0, 0);
    add_member(&writer, "top/share/file.txt", '0', payload, sizeof(payload) - 1,
               sizeof(payload) - 1);
    finish(&writer);
    assert(native_sdk_extract_tar_gz(archive, output, error, sizeof(error)));
    snprintf(path, sizeof(path), "%s/share/file.txt", output);
    assert(native_cache_file_digest(path, digest));

    snprintf(path, sizeof(path), "%s/share/budo", output);
    assert(native_cache_make_directories(path));
    snprintf(manifest, sizeof(manifest), "%s/budo-native-sdk.json", path);
    {
        char json[4096];
        snprintf(json, sizeof(json),
                 "{\"budo_version\":\"0.4.3\",\"compiler_family\":\"test\","
                 "\"files\":[{\"path\":\"share/file.txt\",\"sha256\":\"%s\","
                 "\"size\":5}],\"native_api_version\":\"1.0\","
                 "\"schema_version\":1,\"sdk_input_digest\":"
                 "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                 "\"target_tuple\":\"%s\"}\n",
                 digest, BUDO_LOCAL_TARGET_TUPLE);
        write_text(manifest, json);
    }
    assert(native_sdk_verify_directory(output, &selection, error, sizeof(error)));
    snprintf(path, sizeof(path), "%s/extra", output);
    write_text(path, "not manifested\n");
    assert(!native_sdk_verify_directory(output, &selection, error, sizeof(error)));
    remove(path);
    write_text(path, "");
    remove(path);
    snprintf(path, sizeof(path), "%s/share/file.txt", output);
    write_text(path, "evil\n");
    assert(!native_sdk_verify_directory(output, &selection, error, sizeof(error)));

    puts("native SDK security tests passed");
    return 0;
}