#ifndef DEVICE_WRAPPER_H
#define DEVICE_WRAPPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool device_keep_screen_on(bool enabled);

    bool device_is_screen_kept_on(void);

#ifdef __cplusplus
}
#endif

#endif