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

#endif