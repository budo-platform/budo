#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096 
#endif

#include "core/android_package.h"
#include "core/budo_init.h"
#include "core/cli.h"
#include "core/embedded_resource.h"
#include "core/fs_util.h"
#include "core/web_export.h"
#include "core/web_server.h"
#include "desktop/managed_desktop.h"
#include "native/native_compile.h"

#include "embedded_devapi.h"
#include "embedded_types.h"

int budo_cli_main(int argc, char **argv)
{
    BudoCliOptions options;
    if (!budo_cli_parse(argc, argv, &options))
        return 1;

    switch (options.command)
    {
    case BUDO_CMD_HELP:
        budo_cli_print_usage(argv[0]);
        return 0;

    case BUDO_CMD_VERSION:
        budo_cli_print_version();
        return 0;

    case BUDO_CMD_PRINT_DTS:
        return embedded_resource_write_stdout(embedded_types_data, embedded_types_len,
                                              embedded_types_uncompressed_len,
                                              embedded_types_is_gzip) == 0
                   ? 0
                   : 1;

    case BUDO_CMD_PRINT_DEVAPI:
        return embedded_resource_write_stdout(embedded_devapi_data, embedded_devapi_len,
                                              embedded_devapi_uncompressed_len,
                                              embedded_devapi_is_gzip) == 0
                   ? 0
                   : 1;

    case BUDO_CMD_INIT:
        return budo_run_init(options.project_dir, &options.init);

    case BUDO_CMD_COMPILE:
        return native_compile_project(options.project_dir, &options.compile);

    case BUDO_CMD_NATIVE_CACHE_INSPECT:
        return native_compile_cache_inspect(options.cache_json);

    case BUDO_CMD_NATIVE_CACHE_CLEAN:
        return native_compile_cache_clean_all();

    case BUDO_CMD_NATIVE_SDK_CACHE_INSPECT:
        return native_compile_sdk_cache_inspect(options.cache_json);

    case BUDO_CMD_NATIVE_SDK_CACHE_CLEAN:
        return native_compile_sdk_cache_clean_all();

    case BUDO_CMD_ANDROID_CACHE_INSPECT:
        return android_build_cache_inspect(options.cache_json);

    case BUDO_CMD_ANDROID_CACHE_CLEAN:
        return android_build_cache_clean(options.cache_android_package);

    case BUDO_CMD_WEB_EXPORT:
        return web_export(options.project_dir, options.web_export_output);

    case BUDO_CMD_ANDROID_APK:
    case BUDO_CMD_ANDROID_AAB:
        return android_package(options.project_dir, &options.android);

    case BUDO_CMD_WEB_SERVE:
    {

        char out_dir[PATH_MAX];
        if (!fs_make_temp_dir("budo-serve", out_dir, sizeof(out_dir)))
        {
            fprintf(stderr, "Error: cannot create temp directory for web-serve: %s\n",
                    strerror(errno));
            return 1;
        }
        int rc = web_export(options.project_dir, out_dir);
        if (rc == 0)
            rc = web_server_serve_directory(out_dir, options.web_listen_host,
                                            options.web_listen_port);
        if (!fs_remove_tree(out_dir))
            fprintf(stderr, "Warning: could not remove '%s'\n", out_dir);
        return rc;
    }

    case BUDO_CMD_RUN:
        break;
    }

    return managed_desktop_run(&options.run);
}