#ifndef BUDO_NATIVE_PROJECT_H
#define BUDO_NATIVE_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BUDO_NATIVE_PROJECT_SCHEMA_VERSION 1
#define BUDO_NATIVE_PROJECT_MAX_PATH 1024
#define BUDO_NATIVE_PROJECT_MAX_NAME 128
#define BUDO_NATIVE_PROJECT_MAX_VALUE 256
#define BUDO_NATIVE_PROJECT_MAX_ITEMS 128

    typedef struct NativeProjectPathList
    {
        size_t count;
        char items[BUDO_NATIVE_PROJECT_MAX_ITEMS][BUDO_NATIVE_PROJECT_MAX_PATH];
    } NativeProjectPathList;

    typedef struct NativeProjectValueList
    {
        size_t count;
        char items[BUDO_NATIVE_PROJECT_MAX_ITEMS][BUDO_NATIVE_PROJECT_MAX_VALUE];
    } NativeProjectValueList;

    typedef struct NativeProject
    {
        int schema_version;
        int c_standard;
        bool single_file;
        char project_root[BUDO_NATIVE_PROJECT_MAX_PATH];
        char manifest_path[BUDO_NATIVE_PROJECT_MAX_PATH];
        char name[BUDO_NATIVE_PROJECT_MAX_NAME];
        char output_name[BUDO_NATIVE_PROJECT_MAX_NAME];
        char sdk_version[BUDO_NATIVE_PROJECT_MAX_VALUE];
        NativeProjectPathList sources;
        NativeProjectPathList include_directories;
        NativeProjectPathList assets;
        NativeProjectValueList definitions;
        NativeProjectValueList modules;
    } NativeProject;

    typedef struct NativeProjectError
    {
        char message[512];
    } NativeProjectError;

    bool native_project_load(const char *input_path, NativeProject *project,
                             NativeProjectError *error);

    bool native_project_generate(const NativeProject *project,
                                 const char *generated_directory,
                                 const char *native_targets_file,
                                 NativeProjectError *error);

    bool native_project_stage_assets(const NativeProject *project,
                                     const char *runtime_directory,
                                     NativeProjectError *error);

    bool native_project_generate_android(const NativeProject *project,
                                         const char *staging_directory,
                                         NativeProjectError *error);

#ifdef __cplusplus
}
#endif

#endif