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