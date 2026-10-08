#ifndef DEVICE_WRAPPER_H
#define DEVICE_WRAPPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool device_keep_screen_on(bool enabled);

    bool device_is_screen_kept_on(void);

    bool device_set_clipboard_text(const char *utf8);
    char *device_get_clipboard_text(void);

    typedef enum
    {
        DEVICE_HAPTIC_LIGHT = 0,
        DEVICE_HAPTIC_MEDIUM,
        DEVICE_HAPTIC_HEAVY,
        DEVICE_HAPTIC_SELECTION,
        DEVICE_HAPTIC_SUCCESS,
        DEVICE_HAPTIC_WARNING,
        DEVICE_HAPTIC_ERROR,
        DEVICE_HAPTIC_COUNT
    } DeviceHaptic;
    bool device_haptic(DeviceHaptic kind);

    typedef struct
    {
        bool dark_mode;
        bool reduced_motion;
        bool high_contrast;
        float font_scale;
        float safe_top;
        float safe_right;
        float safe_bottom;
        float safe_left;

        float keyboard_inset;
    } DevicePreferences;
    void device_get_preferences(DevicePreferences *out);

    typedef enum
    {
        DEVICE_CURSOR_DEFAULT = 0,
        DEVICE_CURSOR_TEXT,
        DEVICE_CURSOR_POINTER,
        DEVICE_CURSOR_GRAB,
        DEVICE_CURSOR_GRABBING,
        DEVICE_CURSOR_MOVE,
        DEVICE_CURSOR_RESIZE_EW,
        DEVICE_CURSOR_RESIZE_NS,
        DEVICE_CURSOR_RESIZE_NWSE,
        DEVICE_CURSOR_RESIZE_NESW,
        DEVICE_CURSOR_NOT_ALLOWED,
        DEVICE_CURSOR_WAIT,
        DEVICE_CURSOR_CROSSHAIR,
        DEVICE_CURSOR_NONE,
        DEVICE_CURSOR_COUNT
    } DeviceCursor;
    bool device_set_cursor(DeviceCursor cursor);

#ifdef __cplusplus
}
#endif

#endif