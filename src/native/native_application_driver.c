#include "native_application_driver.h"
#include "native_host_internal.h"

#include <stddef.h>
#include <string.h>

static bool native_initialize(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    BudoStatus status;
    const BudoError *error;

    budo_host_set_error(adapter->host, BUDO_STATUS_OK, "");
    budo_native_host_enter_callback(adapter->host, false);
    status = adapter->application->initialize(adapter->host,
                                              &adapter->app_state);
    budo_native_host_leave_callback(adapter->host);
    if (status == BUDO_STATUS_OK)
        return true;

    error = budo_host_last_error(adapter->host);
    if (!error || error->status == BUDO_STATUS_OK)
        budo_host_set_error(adapter->host, status,
                            "Native application initialization failed");
    return false;
}

static void native_surface_created(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    budo_native_host_surface_created(adapter->host);
    budo_native_host_enter_callback(adapter->host, true);
    if (adapter->application->surface_created)
        adapter->application->surface_created(adapter->host,
                                              adapter->app_state);
    budo_native_host_leave_callback(adapter->host);
}

static void native_resize(void *opaque, int width, int height, float density)
{
    NativeApplicationDriver *adapter = opaque;

    adapter->surface.struct_size = sizeof(adapter->surface);
    adapter->surface.width = (uint32_t)width;
    adapter->surface.height = (uint32_t)height;
    adapter->surface.density = density;
    budo_native_host_enter_callback(adapter->host, true);
    if (adapter->application->resize)
        adapter->application->resize(adapter->host, adapter->app_state,
                                     &adapter->surface);
    budo_native_host_leave_callback(adapter->host);
}

static void native_frame(void *opaque, double timestamp_ms,
                         double delta_seconds)
{
    NativeApplicationDriver *adapter = opaque;
    BudoFrameInfo frame;

    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.timestamp_ms = timestamp_ms;
    frame.delta_seconds = delta_seconds;
    frame.frame_index = adapter->frame_index++;
    frame.surface = adapter->surface;
    frame.input = budo_host_input(adapter->host);
    budo_native_host_enter_callback(adapter->host, true);
    adapter->application->frame(adapter->host, adapter->app_state, &frame);
    budo_native_host_leave_callback(adapter->host);
}

static void native_pause(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    budo_native_host_enter_callback(adapter->host, false);
    if (adapter->application->pause)
        adapter->application->pause(adapter->host, adapter->app_state);
    budo_native_host_leave_callback(adapter->host);
}

static void native_resume(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    budo_native_host_enter_callback(adapter->host, false);
    if (adapter->application->resume)
        adapter->application->resume(adapter->host, adapter->app_state);
    budo_native_host_leave_callback(adapter->host);
}

static void native_context_lost(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    budo_native_host_enter_callback(adapter->host, true);
    if (adapter->application->context_lost)
        adapter->application->context_lost(adapter->host,
                                           adapter->app_state);
    budo_native_host_leave_callback(adapter->host);
    budo_native_host_context_lost(adapter->host);
    memset(&adapter->surface, 0, sizeof(adapter->surface));
    adapter->surface.struct_size = sizeof(adapter->surface);
    adapter->surface.density = 1.0f;
}

static void native_shutdown(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    budo_native_host_enter_callback(adapter->host, false);
    if (adapter->application->shutdown)
        adapter->application->shutdown(adapter->host, adapter->app_state);
    budo_native_host_leave_callback(adapter->host);
    adapter->app_state = NULL;
}

static void native_destroy(void *opaque)
{
    NativeApplicationDriver *adapter = opaque;
    adapter->application = NULL;
    adapter->host = NULL;
    adapter->app_state = NULL;
}

static const ApplicationDriverOps native_ops = {
    .initialize = native_initialize,
    .surface_created = native_surface_created,
    .resize = native_resize,
    .frame = native_frame,
    .pause = native_pause,
    .resume = native_resume,
    .context_lost = native_context_lost,
    .shutdown = native_shutdown,
    .destroy = native_destroy,
};

static BudoStatus validation_error(BudoHost *host, BudoStatus status,
                                   const char *message)
{
    budo_host_set_error(host, status, message);
    return status;
}

BudoStatus native_application_driver_init(ApplicationDriver *driver,
                                          NativeApplicationDriver *adapter,
                                          const BudoApplication *application,
                                          BudoHost *host)
{
    const BudoBuildIdentity *identity;
    const size_t required_size = offsetof(BudoApplication, shutdown) +
                                 sizeof(application->shutdown);

    if (!driver || !adapter || !host || !application)
        return validation_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Native application descriptor is missing");
    if (application->struct_size < required_size)
        return validation_error(host, BUDO_STATUS_INCOMPATIBLE_API,
                                "Native application descriptor is too small");
    if (application->api_version != BUDO_NATIVE_API_VERSION)
        return validation_error(host, BUDO_STATUS_INCOMPATIBLE_API,
                                "Native application API version does not match the host");
    identity = budo_host_build_identity(host);
    if (!identity || !application->sdk_version || !application->sdk_build_id ||
        strcmp(application->sdk_version, identity->sdk_version) != 0 ||
        strcmp(application->sdk_build_id, identity->sdk_build_id) != 0)
        return validation_error(host, BUDO_STATUS_INCOMPATIBLE_API,
                                "Native application SDK identity does not match the host");
    if (!application->initialize || !application->frame)
        return validation_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Native application requires initialize and frame callbacks");

    memset(adapter, 0, sizeof(*adapter));
    adapter->application = application;
    adapter->host = host;
    adapter->surface.struct_size = sizeof(adapter->surface);
    adapter->surface.density = 1.0f;
    budo_host_set_error(host, BUDO_STATUS_OK, "");
    application_driver_init(driver, &native_ops, adapter);
    return BUDO_STATUS_OK;
}