#ifndef BUDO_WINDOW_MACOS_PINCH_H
#define BUDO_WINDOW_MACOS_PINCH_H

#include "input.h"
#include <SDL2/SDL.h>

typedef struct MacOSPinchMonitor MacOSPinchMonitor;

MacOSPinchMonitor *macos_pinch_install(SDL_Window *window);
void macos_pinch_remove(MacOSPinchMonitor *monitor);
void macos_pinch_drain(MacOSPinchMonitor *monitor, InputState *input, float density);

#endif