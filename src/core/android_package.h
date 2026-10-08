#ifndef BUDO_ANDROID_PACKAGE_H
#define BUDO_ANDROID_PACKAGE_H

#include <stdbool.h>

typedef struct
{
    bool aab;               
    bool release;           
    bool no_build;          
    bool clean;             
    bool install;           
    const char *output_dir; 
} AndroidPackageOptions;

int android_package(const char *app_dir, const AndroidPackageOptions *opts);

int android_build_cache_inspect(bool json);
int android_build_cache_clean(const char *package_name);

#endif