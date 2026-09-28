#ifndef NETWORK_REQUEST_CORE_H
#define NETWORK_REQUEST_CORE_H

#include "network_wrapper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char url[NETWORK_MAX_URL_SIZE];
    const char *method;
    const uint8_t *body;
    size_t body_len;
    NetworkHeader headers[NETWORK_MAX_HEADERS];
    int header_count;
} NetworkRequestPlan;

bool network_request_validate(const char *method, size_t method_capacity,
                              const char *url,
                              const NetworkHeader *headers, int header_count,
                              const uint8_t *body, size_t body_len,
                              char *error, size_t error_size);

bool network_request_plan_init(NetworkRequestPlan *plan,
                               const char *method, const char *url,
                               const NetworkHeader *headers, int header_count,
                               const uint8_t *body, size_t body_len);

bool network_redirect_status(int status);

bool network_request_plan_redirect(NetworkRequestPlan *plan, int status,
                                   const char *location);

#endif