#ifndef BUDO_NETWORK_SERVICE_H
#define BUDO_NETWORK_SERVICE_H

#include "core/api_error.h"
#include "network_wrapper.h"

NetworkResponse *network_service_request(NetworkContext *context,
                                         const char *method, const char *url,
                                         const NetworkHeader *headers,
                                         int header_count,
                                         const uint8_t *body, size_t body_length,
                                         ApiError *error);
int network_service_request_async(NetworkContext *context,
                                  const char *method, const char *url,
                                  const NetworkHeader *headers,
                                  int header_count,
                                  const uint8_t *body, size_t body_length,
                                  NetworkAsyncCallback callback, void *user_data,
                                  ApiError *error);

#endif