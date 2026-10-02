#ifndef BUDO_PUBLIC_VERSION_H
#define BUDO_PUBLIC_VERSION_H

#include <stdint.h>

#define BUDO_VERSION_MAJOR 0
#define BUDO_VERSION_MINOR 4
#define BUDO_VERSION_PATCH 3
#define BUDO_VERSION_STRING "0.4.3"

#define BUDO_NATIVE_API_VERSION_MAJOR 1u
#define BUDO_NATIVE_API_VERSION_MINOR 5u
#define BUDO_NATIVE_API_VERSION \
    ((BUDO_NATIVE_API_VERSION_MAJOR << 16u) | BUDO_NATIVE_API_VERSION_MINOR)

#ifndef BUDO_NATIVE_SDK_BUILD_ID
#define BUDO_NATIVE_SDK_BUILD_ID "development"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct BudoBuildIdentity
    {
        uint32_t struct_size;
        uint32_t api_version;
        const char *sdk_version;
        const char *sdk_build_id;
    } BudoBuildIdentity;

#ifdef __cplusplus
}
#endif

#endif