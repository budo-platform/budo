#ifndef BUDO_DEVICE_SERVICE_H
#define BUDO_DEVICE_SERVICE_H

#include "core/api_error.h"
#include "device/device_wrapper.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct DeviceContext DeviceContext;

    DeviceContext *device_binding_state_create(void);
    void device_binding_state_destroy(DeviceContext *state);
    bool device_binding_state_keep_screen_on(DeviceContext *state,
                                             bool enabled,
                                             ApiError *error);
    bool device_binding_state_owns_screen_request(
        const DeviceContext *state);

    bool device_service_keep_screen_on(bool enabled, ApiError *error);

    bool device_haptic_from_name(const char *name, DeviceHaptic *out);
    bool device_cursor_from_name(const char *name, DeviceCursor *out);
    const char *device_cursor_name(DeviceCursor cursor);

#ifdef __cplusplus
}
#endif

#endif