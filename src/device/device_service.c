#include "device_service.h"

#include "device_wrapper.h"

#include <stdlib.h>
#include <string.h>

static size_t active_screen_request_count;

struct DeviceContext
{
    bool keep_screen_on;
};

DeviceContext *device_binding_state_create(void)
{
    return (DeviceContext *)calloc(1, sizeof(DeviceContext));
}

void device_binding_state_destroy(DeviceContext *state)
{
    ApiError error;

    if (!state)
        return;
    if (state->keep_screen_on)
    {
        if (active_screen_request_count > 0)
            active_screen_request_count--;
        if (active_screen_request_count == 0)
            device_service_keep_screen_on(false, &error);
    }
    state->keep_screen_on = false;
    free(state);
}

bool device_binding_state_keep_screen_on(DeviceContext *state,
                                         bool enabled, ApiError *error)
{
    api_error_clear(error);
    if (!state)
    {
        api_error_set(error, API_STATUS_INVALID_STATE,
                      "device.invalid_state",
                      "Device binding state is required");
        return false;
    }
    if (state->keep_screen_on == enabled)
        return true;

    if (enabled)
    {
        if (active_screen_request_count == 0 &&
            !device_service_keep_screen_on(true, error))
            return false;
        active_screen_request_count++;
    }
    else
    {
        if (active_screen_request_count == 0)
        {
            api_error_set(error, API_STATUS_INVALID_STATE,
                          "device.request_count_invalid",
                          "Keep-screen-on ownership is inconsistent");
            return false;
        }
        if (active_screen_request_count == 1 &&
            !device_service_keep_screen_on(false, error))
            return false;
        active_screen_request_count--;
    }
    state->keep_screen_on = enabled;
    return true;
}

bool device_binding_state_owns_screen_request(const DeviceContext *state)
{
    return state && state->keep_screen_on;
}

bool device_service_keep_screen_on(bool enabled, ApiError *error)
{
    api_error_clear(error);
    if (device_keep_screen_on(enabled))
        return true;

    api_error_set(error, API_STATUS_APPLICATION_ERROR,
                  "device.keep_screen_on_failed",
                  "The platform could not update the keep-screen-on request");
    return false;
}

static const char *const haptic_names[DEVICE_HAPTIC_COUNT] = {
    "light", "medium", "heavy", "selection", "success", "warning", "error"};

static const char *const cursor_names[DEVICE_CURSOR_COUNT] = {
    "default", "text", "pointer", "grab", "grabbing", "move", "ew-resize", "ns-resize",
    "nwse-resize", "nesw-resize", "not-allowed", "wait", "crosshair", "none"};

bool device_haptic_from_name(const char *name, DeviceHaptic *out)
{
    for (int i = 0; name && i < DEVICE_HAPTIC_COUNT; i++)
        if (strcmp(name, haptic_names[i]) == 0)
        {
            *out = (DeviceHaptic)i;
            return true;
        }
    return false;
}

bool device_cursor_from_name(const char *name, DeviceCursor *out)
{
    for (int i = 0; name && i < DEVICE_CURSOR_COUNT; i++)
        if (strcmp(name, cursor_names[i]) == 0)
        {
            *out = (DeviceCursor)i;
            return true;
        }
    return false;
}

const char *device_cursor_name(DeviceCursor cursor)
{
    return (int)cursor >= 0 && cursor < DEVICE_CURSOR_COUNT ? cursor_names[cursor] : "default";
}