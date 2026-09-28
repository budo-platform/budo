#ifndef BUDO_EMBEDDED_RESOURCE_H
#define BUDO_EMBEDDED_RESOURCE_H

#include <stddef.h>

int embedded_resource_unpack(const unsigned char *data,
                             size_t len,
                             size_t uncompressed_len,
                             int is_gzip,
                             unsigned char **out_data,
                             size_t *out_len);

int embedded_resource_write_file(const char *path,
                                 const unsigned char *data,
                                 size_t len,
                                 size_t uncompressed_len,
                                 int is_gzip,
                                 size_t *out_len);

int embedded_resource_write_stdout(const unsigned char *data,
                                   size_t len,
                                   size_t uncompressed_len,
                                   int is_gzip);

#endif