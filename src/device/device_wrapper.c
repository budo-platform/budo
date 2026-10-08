#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 
#endif

#include "device_wrapper.h"

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

bool device_set_clipboard_text(const char *utf8)
{
    return utf8 && SDL_SetClipboardText(utf8) == 0;
}

char *device_get_clipboard_text(void)
{
    if (!SDL_HasClipboardText())
        return NULL;
    char *text = SDL_GetClipboardText();
    if (!text)
        return NULL;
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy)
        memcpy(copy, text, length + 1);
    SDL_free(text);
    return copy;
}

bool device_haptic(DeviceHaptic kind)
{
    (void)kind;
    return false;
}

#ifdef __APPLE__
static bool mac_bool(CFStringRef key, CFStringRef application)
{
    Boolean valid = false;
    Boolean value = CFPreferencesGetAppBooleanValue(key, application, &valid);
    return valid && value;
}

static void read_preferences(DevicePreferences *out)
{
    CFStringRef universal_access = CFSTR("com.apple.universalaccess");
    CFPreferencesAppSynchronize(kCFPreferencesAnyApplication);
    CFPreferencesAppSynchronize(universal_access);
    CFPropertyListRef style = CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesAnyApplication);
    if (style)
    {
        out->dark_mode = CFGetTypeID(style) == CFStringGetTypeID() &&
                         CFStringCompare((CFStringRef)style, CFSTR("Dark"), kCFCompareCaseInsensitive) == kCFCompareEqualTo;
        CFRelease(style);
    }
    out->reduced_motion = mac_bool(CFSTR("reduceMotion"), universal_access);
    out->high_contrast = mac_bool(CFSTR("increaseContrast"), universal_access);
}
#elif defined(_WIN32)
static bool windows_dword(HKEY root, const wchar_t *path, const wchar_t *name, DWORD *value)
{
    DWORD size = sizeof(*value);
    return RegGetValueW(root, path, name, RRF_RT_REG_DWORD, NULL, value, &size) == ERROR_SUCCESS;
}

static void read_preferences(DevicePreferences *out)
{
    DWORD value = 1;
    if (windows_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      L"AppsUseLightTheme", &value))
        out->dark_mode = value == 0;
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        out->reduced_motion = !animations;
    HIGHCONTRASTW contrast = {sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
        out->high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (windows_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", &value) &&
        value >= 100 && value <= 225)
        out->font_scale = (float)value / 100.0f;
}
#else
#include <stdio.h>

static bool gsettings_get(const char *schema, const char *key, char *out, size_t size)
{
    char command[256];
    snprintf(command, sizeof(command), "gsettings get %s %s 2>/dev/null", schema, key);
    FILE *pipe = popen(command, "r");
    if (!pipe)
        return false;
    bool ok = fgets(out, (int)size, pipe) != NULL;
    int status = pclose(pipe);
    return ok && status == 0;
}

static void read_preferences(DevicePreferences *out)
{
    
    static DevicePreferences cached;
    static Uint32 read_at;
    static bool have;
    Uint32 now = SDL_GetTicks();
    if (have && now - read_at < 5000)
    {
        *out = cached;
        return;
    }
    char value[128];
    const char *theme = getenv("GTK_THEME");
    out->dark_mode = theme && (strstr(theme, "dark") || strstr(theme, "Dark"));
    if (gsettings_get("org.gnome.desktop.interface", "color-scheme", value, sizeof(value)))
        out->dark_mode = strstr(value, "prefer-dark") != NULL;
    else if (gsettings_get("org.gnome.desktop.interface", "gtk-theme", value, sizeof(value)))
        out->dark_mode = out->dark_mode || strstr(value, "dark") || strstr(value, "Dark");
    if (gsettings_get("org.gnome.desktop.interface", "enable-animations", value, sizeof(value)))
        out->reduced_motion = strncmp(value, "false", 5) == 0;
    if (gsettings_get("org.gnome.desktop.a11y.interface", "high-contrast", value, sizeof(value)))
        out->high_contrast = strncmp(value, "true", 4) == 0;
    if (gsettings_get("org.gnome.desktop.interface", "text-scaling-factor", value, sizeof(value)) &&
        atof(value) > 0.5 && atof(value) < 4.0)
        out->font_scale = (float)atof(value);
    const char *motion = getenv("BUDO_REDUCED_MOTION");
    const char *scale = getenv("BUDO_FONT_SCALE");
    if (motion)
        out->reduced_motion = strcmp(motion, "0") != 0;
    if (scale && atof(scale) > 0.5 && atof(scale) < 4.0)
        out->font_scale = (float)atof(scale);
    cached = *out;
    read_at = now;
    have = true;
}
#endif

void device_get_preferences(DevicePreferences *out)
{
    
    static DevicePreferences cached;
    static Uint32 read_at;
    static bool have;
    if (!out)
        return;
    Uint32 now = SDL_GetTicks();
    if (!have || now - read_at > 500)
    {
        memset(&cached, 0, sizeof(cached));
        cached.font_scale = 1.0f;
        read_preferences(&cached);
        read_at = now;
        have = true;
    }
    *out = cached;
}

bool device_set_cursor(DeviceCursor cursor)
{
    static SDL_Cursor *cursors[DEVICE_CURSOR_COUNT];
    static const SDL_SystemCursor system[DEVICE_CURSOR_COUNT] = {
        [DEVICE_CURSOR_DEFAULT] = SDL_SYSTEM_CURSOR_ARROW,
        [DEVICE_CURSOR_TEXT] = SDL_SYSTEM_CURSOR_IBEAM,
        [DEVICE_CURSOR_POINTER] = SDL_SYSTEM_CURSOR_HAND,
        [DEVICE_CURSOR_GRAB] = SDL_SYSTEM_CURSOR_HAND,
        [DEVICE_CURSOR_GRABBING] = SDL_SYSTEM_CURSOR_SIZEALL,
        [DEVICE_CURSOR_MOVE] = SDL_SYSTEM_CURSOR_SIZEALL,
        [DEVICE_CURSOR_RESIZE_EW] = SDL_SYSTEM_CURSOR_SIZEWE,
        [DEVICE_CURSOR_RESIZE_NS] = SDL_SYSTEM_CURSOR_SIZENS,
        [DEVICE_CURSOR_RESIZE_NWSE] = SDL_SYSTEM_CURSOR_SIZENWSE,
        [DEVICE_CURSOR_RESIZE_NESW] = SDL_SYSTEM_CURSOR_SIZENESW,
        [DEVICE_CURSOR_NOT_ALLOWED] = SDL_SYSTEM_CURSOR_NO,
        [DEVICE_CURSOR_WAIT] = SDL_SYSTEM_CURSOR_WAIT,
        [DEVICE_CURSOR_CROSSHAIR] = SDL_SYSTEM_CURSOR_CROSSHAIR,
        [DEVICE_CURSOR_NONE] = SDL_SYSTEM_CURSOR_ARROW,
    };
    if ((int)cursor < 0 || cursor >= DEVICE_CURSOR_COUNT || !SDL_WasInit(SDL_INIT_VIDEO))
        return false;
    SDL_ShowCursor(cursor == DEVICE_CURSOR_NONE ? SDL_DISABLE : SDL_ENABLE);
    if (cursor == DEVICE_CURSOR_NONE)
        return true;
    if (!cursors[cursor])
        cursors[cursor] = SDL_CreateSystemCursor(system[cursor]);
    if (!cursors[cursor])
        return false;
    if (SDL_GetCursor() != cursors[cursor])
        SDL_SetCursor(cursors[cursor]);
    return true;
}