#ifndef BUDO_APPLICATION_DRIVER_H
#define BUDO_APPLICATION_DRIVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ApplicationDriverOps
{

    bool (*initialize)(void *context);
    void (*surface_created)(void *context);
    void (*resize)(void *context, int width, int height, float density);
    void (*frame)(void *context, double timestamp_ms, double delta_seconds);
    void (*pause)(void *context);
    void (*resume)(void *context);
    void (*context_lost)(void *context);
    void (*shutdown)(void *context);
    void (*destroy)(void *context);
} ApplicationDriverOps;

typedef struct ApplicationDriver
{
    const ApplicationDriverOps *ops;
    void *context;
    bool initialization_started;
    bool initialized;
    bool surface_ready;
    bool paused;
    bool shutdown_called;
    bool destroyed;
    int width;
    int height;
    float density;
} ApplicationDriver;

void application_driver_init(ApplicationDriver *driver,
                             const ApplicationDriverOps *ops,
                             void *context);
bool application_driver_initialize(ApplicationDriver *driver);
void application_driver_surface_created(ApplicationDriver *driver);
void application_driver_resize(ApplicationDriver *driver,
                               int width, int height, float density);
void application_driver_frame(ApplicationDriver *driver,
                              double timestamp_ms, double delta_seconds);
void application_driver_pause(ApplicationDriver *driver);
void application_driver_resume(ApplicationDriver *driver);
void application_driver_context_lost(ApplicationDriver *driver);
void application_driver_shutdown(ApplicationDriver *driver);
void application_driver_destroy(ApplicationDriver *driver);
bool application_driver_is_ready(const ApplicationDriver *driver);

#ifdef __cplusplus
}
#endif

#endif