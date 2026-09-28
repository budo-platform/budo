#ifndef BUDO_FILE_SERVICE_H
#define BUDO_FILE_SERVICE_H

#include "core/api_error.h"
#include "file_wrapper.h"

char *file_service_read_text_virtual(FileContext *context, const char *path,
                                     size_t *length, ApiError *error);
char *file_service_read_text(FileContext *context, FileRoot root,
                             const char *path, size_t *length,
                             ApiError *error);
bool file_service_exists(FileContext *context, FileRoot root,
                         const char *path, ApiError *error);

#endif