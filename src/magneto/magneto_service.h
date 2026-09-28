#ifndef BUDO_MAGNETO_SERVICE_H
#define BUDO_MAGNETO_SERVICE_H

#include "core/api_error.h"
#include "magneto_wrapper.h"

bool magneto_service_is_available(MagnetoContext *context, ApiError *error);
bool magneto_service_start(MagnetoContext *context, ApiError *error);
void magneto_service_stop(MagnetoContext *context, ApiError *error);
bool magneto_service_get_accel(MagnetoContext *context,
                               MagnetoAccelData *data, ApiError *error);

#endif