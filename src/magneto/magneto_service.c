#include "magneto_service.h"

static bool magneto_service_context(MagnetoContext *context, ApiError *error)
{
    api_error_clear(error);
    if (context)
        return true;
    api_error_set(error, API_STATUS_UNSUPPORTED,
                  "magneto.unavailable", "Sensors are unavailable");
    return false;
}

bool magneto_service_is_available(MagnetoContext *context, ApiError *error)
{
    api_error_clear(error);
    return context && magneto_is_available(context);
}

bool magneto_service_start(MagnetoContext *context, ApiError *error)
{
    if (!magneto_service_context(context, error))
        return false;
    if (magneto_start(context))
        return true;
    api_error_set(error, API_STATUS_APPLICATION_ERROR,
                  "magneto.start_failed", "Could not start sensor updates");
    return false;
}

void magneto_service_stop(MagnetoContext *context, ApiError *error)
{
    api_error_clear(error);
    if (context)
        magneto_stop(context);
}

bool magneto_service_get_accel(MagnetoContext *context,
                               MagnetoAccelData *data, ApiError *error)
{
    if (!data)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "magneto.invalid_output", "Acceleration output is required");
        return false;
    }
    if (!magneto_service_context(context, error))
        return false;
    if (magneto_get_accel(context, data))
        return true;
    api_error_set(error, API_STATUS_APPLICATION_ERROR,
                  "magneto.data_unavailable",
                  "Acceleration data is not available yet");
    return false;
}