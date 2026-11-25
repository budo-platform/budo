#ifndef BUDO_DESKTOP_RECORDING_H
#define BUDO_DESKTOP_RECORDING_H

#include "core/window.h"

typedef struct DesktopRecording DesktopRecording;

DesktopRecording *desktop_recording_start(Window *window, const char *path);

void desktop_recording_event(void *context, Window *window, const union SDL_Event *event);
bool desktop_recording_ok(DesktopRecording *recording);

bool desktop_recording_finish(DesktopRecording *recording);

#endif