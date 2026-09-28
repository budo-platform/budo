#include "network_service.h"

static bool network_service_validate(NetworkContext *context,
                                     const char *method, const char *url,
                                     int header_count, size_t body_length,
                                     ApiError *error)
{
    api_error_clear(error);
    if (!context)
    {
        api_error_set(error, API_STATUS_INVALID_STATE,
                      "network.invalid_state", "Network context is unavailable");
        return false;
    }
    if (!network_is_enabled(context))
    {
        api_error_set(error, API_STATUS_UNSUPPORTED,
                      "network.disabled", "Network access is disabled");
        return false;
    }
    if (!method || !method[0] || !url || !url[0] || header_count < 0 ||
        header_count > NETWORK_MAX_HEADERS || body_length > NETWORK_MAX_REQUEST_BODY_SIZE)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "network.invalid_request", "Network request is invalid");
        return false;
    }
    return true;
}

static void network_service_wrapper_error(NetworkContext *context,
                                          const char *code, ApiError *error)
{
    const char *message = network_get_error(context);
    api_error_set(error, API_STATUS_APPLICATION_ERROR, code,
                  message && message[0] ? message : "Network request failed");
}

NetworkResponse *network_service_request(NetworkContext *context,
                                         const char *method, const char *url,
                                         const NetworkHeader *headers,
                                         int header_count,
                                         const uint8_t *body, size_t body_length,
                                         ApiError *error)
{
    NetworkResponse *response;
    if (!network_service_validate(context, method, url, header_count,
                                  body_length, error))
        return NULL;
    response = network_request(context, method, url, headers, header_count,
                               body, body_length);
    if (!response)
        network_service_wrapper_error(context, "network.request_failed", error);
    return response;
}

int network_service_request_async(NetworkContext *context,
                                  const char *method, const char *url,
                                  const NetworkHeader *headers,
                                  int header_count,
                                  const uint8_t *body, size_t body_length,
                                  NetworkAsyncCallback callback, void *user_data,
                                  ApiError *error)
{
    int request_id;
    if (!network_service_validate(context, method, url, header_count,
                                  body_length, error) ||
        !callback)
    {
        if (!api_error_has_error(error))
            api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                          "network.invalid_callback",
                          "Network completion callback is required");
        return -1;
    }
    request_id = network_request_async(context, method, url, headers,
                                       header_count, body, body_length,
                                       callback, user_data);
    if (request_id < 0)
        network_service_wrapper_error(context, "network.submit_failed", error);
    return request_id;
}