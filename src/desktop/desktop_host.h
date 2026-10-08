#ifndef BUDO_DESKTOP_HOST_H
#define BUDO_DESKTOP_HOST_H

#include <stdbool.h>

#include "core/application_driver.h"
#include "core/input.h"
#include "core/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct DesktopHostApplication
    {
        void *context;
        ApplicationDriver *(*get_driver)(void *context);
        void (*before_frame)(void *context, Window *window, InputState *input,
                             double now_seconds);
        
        bool (*keep_running)(void *context);

        double (*idle_ms)(void *context);
    } DesktopHostApplication;

    int desktop_host_run(Window *window, InputState *input,
                         const DesktopHostApplication *application);

#ifdef __cplusplus
}
#endif

#endif