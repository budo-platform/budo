#include "device_wrapper.h"

#include <SDL2/SDL.h>

static bool g_device_keep_on = false;

bool device_keep_screen_on(bool enabled)
{
    if (enabled)
        SDL_DisableScreenSaver();
    else
        SDL_EnableScreenSaver();
    g_device_keep_on = enabled;
    return true;
}

bool device_is_screen_kept_on(void)
{
    return g_device_keep_on;
}