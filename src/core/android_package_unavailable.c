#include "core/android_package.h"

#include <stdio.h>

int android_package(const char *app_dir, const AndroidPackageOptions *opts)
{
    (void)app_dir;
    (void)opts;

    fprintf(stderr,
            "Android APK/AAB packaging is a Budo Pro feature.\n"
            "Install the private Android feature pack under private/android, "
            "then rebuild Budo to enable the public packaging commands.\n");
    return 1;
}

int android_build_cache_inspect(bool json)
{
    (void)json;
    fprintf(stderr, "Android packaging is not included in this build; there is no Android build cache.\n");
    return 1;
}

int android_build_cache_clean(const char *package_name)
{
    (void)package_name;
    fprintf(stderr, "Android packaging is not included in this build; there is no Android build cache.\n");
    return 1;
}