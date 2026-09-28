#include "audio_service.h"

static void audio_service_failure(AudioContext *context, const char *code,
                                  const char *fallback, ApiError *error)
{
    const char *message = context ? audio_get_error(context) : fallback;
    if (!message || !message[0])
        message = fallback;
    api_error_set(error, context ? API_STATUS_APPLICATION_ERROR : API_STATUS_INVALID_STATE,
                  code, message);
}

int audio_service_create_oscillator(AudioContext *context, ApiError *error)
{
    int result;
    api_error_clear(error);
    if (!context)
    {
        audio_service_failure(context, "audio.invalid_state",
                              "Audio context is unavailable", error);
        return -1;
    }
    result = audio_create_oscillator(context);
    if (result < 0)
        audio_service_failure(context, "audio.resource_limit",
                              "Could not create oscillator", error);
    return result;
}

int audio_service_load_buffer(AudioContext *context, const char *path,
                              ApiError *error)
{
    int result;
    api_error_clear(error);
    if (!path || !path[0])
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "audio.invalid_path", "Audio path is required");
        return -1;
    }
    if (!context)
    {
        audio_service_failure(context, "audio.invalid_state",
                              "Audio context is unavailable", error);
        return -1;
    }
    result = audio_load_buffer(context, path);
    if (result < 0)
        audio_service_failure(context, "audio.load_failed",
                              "Could not load audio buffer", error);
    return result;
}