#ifndef APP_METADATA_H
#define APP_METADATA_H

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char name[256];
    char author[256];
    char date[64];
    char version[64];
    char orientation[64];
    bool neural_enabled;     
    bool filesystem_enabled; 
    bool valid;              
} AppMetadata;

bool app_metadata_parse_version(const char *json, char *out, size_t out_size,
                                char *error, size_t error_size);

bool app_metadata_load(const char *project_dir, AppMetadata *out);

#endif