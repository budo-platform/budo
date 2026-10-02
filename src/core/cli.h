#ifndef BUDO_CLI_H
#define BUDO_CLI_H

#include <stdbool.h>

#include "core/android_package.h"
#include "core/budo_init.h"
#include "native/native_compile.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum BudoCommand
    {
        BUDO_CMD_RUN, 
        BUDO_CMD_COMPILE,
        BUDO_CMD_NATIVE_CACHE_INSPECT,
        BUDO_CMD_NATIVE_CACHE_CLEAN,
        BUDO_CMD_NATIVE_SDK_CACHE_INSPECT,
        BUDO_CMD_NATIVE_SDK_CACHE_CLEAN,
        BUDO_CMD_INIT,
        BUDO_CMD_WEB_SERVE,
        BUDO_CMD_WEB_EXPORT,
        BUDO_CMD_ANDROID_APK,
        BUDO_CMD_ANDROID_AAB,
        BUDO_CMD_PRINT_DTS,
        BUDO_CMD_PRINT_DEVAPI,
        BUDO_CMD_HELP,
        BUDO_CMD_VERSION
    } BudoCommand;

    typedef struct BudoRunOptions
    {
        const char *target;              
        const char *from_input_language; 
        int width;
        int height;
        const char *title;
        bool fullscreen;
        bool vsync;
        bool watch;
        const char *file_root; 
    } BudoRunOptions;

    typedef struct BudoCliOptions
    {
        BudoCommand command;
        const char *project_dir; 
        BudoRunOptions run;
        BudoInitOptions init;
        NativeCompileOptions compile;
        bool cache_json; 
        const char *web_export_output;
        const char *web_listen_host; 
        int web_listen_port;
        char web_listen_host_buf[256];
        AndroidPackageOptions android;
    } BudoCliOptions;

    bool budo_cli_parse(int argc, char **argv, BudoCliOptions *options);
    void budo_cli_print_usage(const char *program_name);
    void budo_cli_print_version(void);

#ifdef __cplusplus
}
#endif

#endif