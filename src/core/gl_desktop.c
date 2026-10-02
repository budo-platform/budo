#include "core/gl_desktop.h"

#ifdef BUDO_GL_DESKTOP_LOADER

#include <SDL2/SDL.h>

#define BUDO_GL_DESKTOP_DEFINE(type, name) type budo_##name = NULL;
BUDO_GL_DESKTOP_FUNCTIONS(BUDO_GL_DESKTOP_DEFINE)
#undef BUDO_GL_DESKTOP_DEFINE

bool budo_gl_desktop_load(void)
{
#define BUDO_GL_DESKTOP_LOAD(type, name)                                   \
    budo_##name = (type)SDL_GL_GetProcAddress(#name);                      \
    if (!budo_##name)                                                      \
    {                                                                      \
        SDL_Log("OpenGL function %s is unavailable (OpenGL 3.3 required)", \
                #name);                                                    \
        return false;                                                      \
    }
    BUDO_GL_DESKTOP_FUNCTIONS(BUDO_GL_DESKTOP_LOAD)
#undef BUDO_GL_DESKTOP_LOAD
    return true;
}

#else

bool budo_gl_desktop_load(void)
{
    return true;
}

#endif