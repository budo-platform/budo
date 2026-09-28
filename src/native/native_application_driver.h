#ifndef BUDO_NATIVE_APPLICATION_DRIVER_H
#define BUDO_NATIVE_APPLICATION_DRIVER_H

#include <budo/application.h>
#include "core/application_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct NativeApplicationDriver
    {
        const BudoApplication *application;
        BudoHost *host;
        void *app_state;
        BudoSurfaceInfo surface;
        uint64_t frame_index;
    } NativeApplicationDriver;

    BudoStatus native_application_driver_init(ApplicationDriver *driver,
                                              NativeApplicationDriver *adapter,
                                              const BudoApplication *application,
                                              BudoHost *host);

#ifdef __cplusplus
}
#endif

#endif