#import <AppKit/AppKit.h>
#include <SDL2/SDL_syswm.h>
#include <math.h>
#include <stdlib.h>

#include "window_macos_pinch.h"

struct MacOSPinchMonitor
{
    void *token;
    NSPoint location;
    double scale;
    Uint32 wake_event;
};

MacOSPinchMonitor *macos_pinch_install(SDL_Window *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        return NULL;

    MacOSPinchMonitor *monitor = calloc(1, sizeof(*monitor));
    if (!monitor)
        return NULL;
    monitor->wake_event = SDL_RegisterEvents(1);
    if (monitor->wake_event == (Uint32)-1)
    {
        free(monitor);
        return NULL;
    }

    __weak NSWindow *target = info.info.cocoa.window;
    id token = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify
        handler:^NSEvent *(NSEvent *event) {
            NSWindow *native_window = target;
            if (!native_window || !native_window.isKeyWindow || event.window != native_window)
                return event;
            const double scale = 1.0 + event.magnification;
            if (!isfinite(scale) || scale <= 0.0 || scale == 1.0)
                return event;
            NSView *view = native_window.contentView;
            if (!view)
                return event;
            const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
            monitor->location = NSMakePoint(point.x - NSMinX(view.bounds),
                view.isFlipped ? point.y - NSMinY(view.bounds) : NSMaxY(view.bounds) - point.y);
            const bool first = monitor->scale == 0.0;
            monitor->scale = first ? scale : monitor->scale * scale;
            if (first)
            {
                // Wake SDL_WaitEventTimeout without sending a wheel or mouse event.
                SDL_Event wake = {0};
                wake.type = monitor->wake_event;
                SDL_PushEvent(&wake);
            }
            return event;
        }];
    if (!token)
    {
        free(monitor);
        return NULL;
    }
    monitor->token = (__bridge_retained void *)token;
    return monitor;
}

void macos_pinch_remove(MacOSPinchMonitor *monitor)
{
    if (!monitor)
        return;
    [NSEvent removeMonitor:(__bridge_transfer id)monitor->token];
    SDL_FlushEvent(monitor->wake_event);
    free(monitor);
}

void macos_pinch_drain(MacOSPinchMonitor *monitor, InputState *input, float density)
{
    if (!monitor || monitor->scale == 0.0)
        return;
    if (input && input->window_focused)
        input_add_pinch(input, (int)lround(monitor->location.x * density),
                       (int)lround(monitor->location.y * density), monitor->scale);
    monitor->scale = 0.0;
}
