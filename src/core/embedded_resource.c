#include "core/embedded_resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int embedded_resource_unpack(const unsigned char *data,
                             size_t len,
                             size_t uncompressed_len,
                             int is_gzip,
                             unsigned char **out_data,
                             size_t *out_len)
{
    if (!data || !out_data || !out_len)
        return -1;

    *out_data = NULL;
    *out_len = 0;

    if (len == 0)
        return 0;

    if (!is_gzip)
    {
        unsigned char *copy = (unsigned char *)malloc(len + 1);
        if (!copy)
            return -1;
        memcpy(copy, data, len);
        copy[len] = '\0';
        *out_data = copy;
        *out_len = len;
        return 0;
    }

    if (uncompressed_len == 0)
        return -1;

    unsigned char *out = (unsigned char *)malloc(uncompressed_len + 1);
    if (!out)
        return -1;

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)data;
    stream.avail_in = (uInt)len;
    stream.next_out = out;
    stream.avail_out = (uInt)uncompressed_len;

    int rc = inflateInit2(&stream, 16 + MAX_WBITS);
    if (rc != Z_OK)
    {
        free(out);
        return -1;
    }

    rc = inflate(&stream, Z_FINISH);
    if (rc != Z_STREAM_END || stream.total_out != uncompressed_len)
    {
        inflateEnd(&stream);
        free(out);
        return -1;
    }
    inflateEnd(&stream);

    out[uncompressed_len] = '\0';
    *out_data = out;
    *out_len = uncompressed_len;
    return 0;
}

int embedded_resource_write_file(const char *path,
                                 const unsigned char *data,
                                 size_t len,
                                 size_t uncompressed_len,
                                 int is_gzip,
                                 size_t *out_len)
{
    unsigned char *unpacked = NULL;
    size_t unpacked_len = 0;
    if (embedded_resource_unpack(data, len, uncompressed_len, is_gzip, &unpacked, &unpacked_len) != 0)
        return -1;

    FILE *file = fopen(path, "wb");
    if (!file)
    {
        free(unpacked);
        return -1;
    }

    int rc = 0;
    if (unpacked_len > 0 && fwrite(unpacked, 1, unpacked_len, file) != unpacked_len)
        rc = -1;
    if (fclose(file) != 0)
        rc = -1;

    if (out_len)
        *out_len = unpacked_len;
    free(unpacked);
    return rc;
}

int embedded_resource_write_stdout(const unsigned char *data,
                                   size_t len,
                                   size_t uncompressed_len,
                                   int is_gzip)
{
    unsigned char *unpacked = NULL;
    size_t unpacked_len = 0;
    if (embedded_resource_unpack(data, len, uncompressed_len, is_gzip, &unpacked, &unpacked_len) != 0)
        return -1;

    int rc = 0;
    if (unpacked_len > 0 && fwrite(unpacked, 1, unpacked_len, stdout) != unpacked_len)
        rc = -1;
    free(unpacked);
    return rc;
}