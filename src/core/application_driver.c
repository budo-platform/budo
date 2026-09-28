#include "application_driver.h"

#include <string.h>

void application_driver_init(ApplicationDriver *driver,
                             const ApplicationDriverOps *ops,
                             void *context)
{
    if (!driver)
        return;

    memset(driver, 0, sizeof(*driver));
    driver->ops = ops;
    driver->context = context;
    driver->density = 1.0f;
}

bool application_driver_initialize(ApplicationDriver *driver)
{
    if (!driver || !driver->ops || !driver->ops->initialize || driver->destroyed ||
        driver->initialization_started)
        return driver && driver->initialized;

    driver->initialization_started = true;
    driver->initialized = driver->ops->initialize(driver->context);
    if (!driver->initialized)
        application_driver_shutdown(driver);
    return driver->initialized;
}

void application_driver_surface_created(ApplicationDriver *driver)
{
    if (!driver || !driver->initialized || driver->destroyed ||
        driver->surface_ready)
        return;

    driver->surface_ready = true;
    if (driver->ops->surface_created)
        driver->ops->surface_created(driver->context);
}

void application_driver_resize(ApplicationDriver *driver,
                               int width, int height, float density)
{
    if (!driver || !driver->initialized || !driver->surface_ready ||
        driver->destroyed || width <= 0 || height <= 0)
        return;

    if (density <= 0.0f)
        density = 1.0f;
    if (driver->width == width && driver->height == height &&
        driver->density == density)
        return;

    driver->width = width;
    driver->height = height;
    driver->density = density;
    if (driver->ops->resize)
        driver->ops->resize(driver->context, width, height, density);
}

void application_driver_frame(ApplicationDriver *driver,
                              double timestamp_ms, double delta_seconds)
{
    if (!application_driver_is_ready(driver))
        return;
    if (driver->ops->frame)
        driver->ops->frame(driver->context, timestamp_ms, delta_seconds);
}

void application_driver_pause(ApplicationDriver *driver)
{
    if (!driver || !driver->initialized || driver->destroyed || driver->paused)
        return;

    driver->paused = true;
    if (driver->ops->pause)
        driver->ops->pause(driver->context);
}

void application_driver_resume(ApplicationDriver *driver)
{
    if (!driver || !driver->initialized || driver->destroyed || !driver->paused)
        return;

    if (driver->ops->resume)
        driver->ops->resume(driver->context);
    driver->paused = false;
}

void application_driver_context_lost(ApplicationDriver *driver)
{
    if (!driver || !driver->initialized || driver->destroyed ||
        !driver->surface_ready)
        return;

    if (driver->ops->context_lost)
        driver->ops->context_lost(driver->context);
    driver->surface_ready = false;
    driver->width = 0;
    driver->height = 0;
}

void application_driver_shutdown(ApplicationDriver *driver)
{
    if (!driver || driver->destroyed || driver->shutdown_called ||
        !driver->initialization_started)
        return;

    driver->shutdown_called = true;
    if (driver->ops && driver->ops->shutdown)
        driver->ops->shutdown(driver->context);
    driver->initialized = false;
    driver->surface_ready = false;
    driver->paused = false;
}

void application_driver_destroy(ApplicationDriver *driver)
{
    if (!driver || driver->destroyed)
        return;

    application_driver_shutdown(driver);
    if (driver->ops && driver->ops->destroy)
        driver->ops->destroy(driver->context);
    driver->destroyed = true;
    driver->ops = NULL;
    driver->context = NULL;
}

bool application_driver_is_ready(const ApplicationDriver *driver)
{
    return driver && driver->initialized && driver->surface_ready &&
           driver->width > 0 && driver->height > 0 &&
           !driver->paused && !driver->destroyed;
}