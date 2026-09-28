#ifndef BUDO_PUBLIC_CORE_H
#define BUDO_PUBLIC_CORE_H

#include <stdint.h>

#include <budo/version.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct BudoHost BudoHost;
    typedef struct BudoInput BudoInput;

    typedef int32_t BudoStatus;
    enum
    {
        BUDO_STATUS_OK = 0,
        BUDO_STATUS_INVALID_ARGUMENT = 1,
        BUDO_STATUS_INCOMPATIBLE_API = 2,
        BUDO_STATUS_UNSUPPORTED = 3,
        BUDO_STATUS_OUT_OF_MEMORY = 4,
        BUDO_STATUS_APPLICATION_ERROR = 5,
        BUDO_STATUS_INTERNAL_ERROR = 6,
        BUDO_STATUS_INVALID_STATE = 7
    };

    typedef int32_t BudoLogLevel;
    enum
    {
        BUDO_LOG_DEBUG = 0,
        BUDO_LOG_INFO = 1,
        BUDO_LOG_WARNING = 2,
        BUDO_LOG_ERROR = 3
    };

    typedef struct BudoError
    {
        uint32_t struct_size;
        BudoStatus status;
        const char *message;
    } BudoError;

    void budo_host_log(BudoHost *host, BudoLogLevel level, const char *message);
    void budo_host_set_error(BudoHost *host, BudoStatus status, const char *message);
    const BudoError *budo_host_last_error(const BudoHost *host);
    const BudoBuildIdentity *budo_host_build_identity(const BudoHost *host);
    const BudoInput *budo_host_input(const BudoHost *host);

#ifdef __cplusplus
}
#endif

#endif