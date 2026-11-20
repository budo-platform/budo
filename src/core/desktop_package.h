#ifndef BUDO_DESKTOP_PACKAGE_H
#define BUDO_DESKTOP_PACKAGE_H

typedef struct
{
    const char *platform;
    const char *name;
    const char *release;
    const char *output;
} DesktopPackageOptions;

#define BUDO_PACKAGE_PATH_SIZE 1024

int desktop_package(const char *project, const DesktopPackageOptions *options);

int desktop_package_prepare(char project[BUDO_PACKAGE_PATH_SIZE],
                            char data[BUDO_PACKAGE_PATH_SIZE]);

#endif