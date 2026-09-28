#ifndef BUDO_API_ERROR_H
#define BUDO_API_ERROR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define API_ERROR_CODE_CAPACITY 64
#define API_ERROR_MESSAGE_CAPACITY 256

    typedef enum ApiStatus
    {
        API_STATUS_OK = 0,
        API_STATUS_INVALID_ARGUMENT = 1,
        API_STATUS_INCOMPATIBLE_API = 2,
        API_STATUS_UNSUPPORTED = 3,
        API_STATUS_OUT_OF_MEMORY = 4,
        API_STATUS_APPLICATION_ERROR = 5,
        API_STATUS_INTERNAL_ERROR = 6,
        API_STATUS_INVALID_STATE = 7
    } ApiStatus;

    typedef struct ApiError
    {
        ApiStatus status;
        char code[API_ERROR_CODE_CAPACITY];
        char message[API_ERROR_MESSAGE_CAPACITY];
    } ApiError;

    void api_error_clear(ApiError *error);
    void api_error_set(ApiError *error, ApiStatus status,
                       const char *code, const char *message);
    bool api_error_has_error(const ApiError *error);
    const char *api_status_name(ApiStatus status);

#ifdef __cplusplus
}
#endif

#endif