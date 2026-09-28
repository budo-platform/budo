#include "file_service.h"

static bool file_service_validate(FileContext *context, const char *path,
                                  ApiError *error)
{
    api_error_clear(error);
    if (!context)
    {
        api_error_set(error, API_STATUS_INVALID_STATE,
                      "file.invalid_state", "File context is unavailable");
        return false;
    }
    if (!file_path_is_valid(path, false))
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "file.invalid_path", "File path is unsafe");
        return false;
    }
    return true;
}

char *file_service_read_text_virtual(FileContext *context, const char *path,
                                     size_t *length, ApiError *error)
{
    char *result;
    api_error_clear(error);
    if (!context)
    {
        api_error_set(error, API_STATUS_INVALID_STATE,
                      "file.invalid_state", "File context is unavailable");
        return NULL;
    }
    if (!path || !path[0])
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "file.invalid_path", "File path is required");
        return NULL;
    }
    result = file_read_text(context, path, length);
    if (!result)
    {
        const char *message = file_get_error(context);
        api_error_set(error, API_STATUS_APPLICATION_ERROR,
                      "file.read_failed",
                      message && message[0] ? message : "Could not read file");
    }
    return result;
}

char *file_service_read_text(FileContext *context, FileRoot root,
                             const char *path, size_t *length,
                             ApiError *error)
{
    char *result;
    if (!file_service_validate(context, path, error))
        return NULL;
    result = file_read_text_at(context, root, path, length);
    if (!result)
    {
        const char *message = file_get_error(context);
        api_error_set(error, API_STATUS_APPLICATION_ERROR,
                      "file.read_failed",
                      message && message[0] ? message : "Could not read file");
    }
    return result;
}

bool file_service_exists(FileContext *context, FileRoot root,
                         const char *path, ApiError *error)
{
    bool result;
    if (!file_service_validate(context, path, error))
        return false;
    result = file_is_file_at(context, root, path);
    if (!result)
        api_error_set(error, API_STATUS_APPLICATION_ERROR,
                      "file.not_found", "File does not exist");
    return result;
}