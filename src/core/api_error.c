#include "api_error.h"

#include <stddef.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t length = 0;

    if (source)
    {
        while (length + 1 < capacity && source[length] != '\0')
            length++;
        memmove(destination, source, length);
    }
    destination[length] = '\0';
}

void api_error_clear(ApiError *error)
{
    if (!error)
        return;

    memset(error, 0, sizeof(*error));
}

void api_error_set(ApiError *error, ApiStatus status,
                   const char *code, const char *message)
{
    if (!error)
        return;
    if (status == API_STATUS_OK)
    {
        api_error_clear(error);
        return;
    }

    error->status = status;
    copy_text(error->code, sizeof(error->code), code);
    copy_text(error->message, sizeof(error->message), message);
}

bool api_error_has_error(const ApiError *error)
{
    return error && error->status != API_STATUS_OK;
}

const char *api_status_name(ApiStatus status)
{
    switch (status)
    {
    case API_STATUS_OK:
        return "ok";
    case API_STATUS_INVALID_ARGUMENT:
        return "invalid_argument";
    case API_STATUS_INCOMPATIBLE_API:
        return "incompatible_api";
    case API_STATUS_UNSUPPORTED:
        return "unsupported";
    case API_STATUS_OUT_OF_MEMORY:
        return "out_of_memory";
    case API_STATUS_APPLICATION_ERROR:
        return "application_error";
    case API_STATUS_INTERNAL_ERROR:
        return "internal_error";
    case API_STATUS_INVALID_STATE:
        return "invalid_state";
    default:
        return "unknown";
    }
}