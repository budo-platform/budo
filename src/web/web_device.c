#include "device/device_wrapper.h"

#include "device/device_service.h"

#include <emscripten.h>
#include <stdbool.h>

EM_JS(int, js_hw_device_keep_screen_on, (int enabled), {
    if (!Module._hw_device)
    {
        Module._hw_device = {lock : null, requested : false};
    }
    var s = Module._hw_device;
    s.requested = enabled ? true : false;

    if (!("wakeLock" in navigator))
    {
        return 0;
    }

    if (enabled)
    {
        if (s.lock)
            return 1;
        navigator.wakeLock.request("screen").then(function(lock) {
                                                s.lock = lock;
                                                lock.addEventListener("release", function() {
                if (s.lock == lock) s.lock = null; });
                                            })
            .catch(function(err){
                
            });

        if (!s.visibilityHandlerInstalled)
        {
            s.visibilityHandlerInstalled = true;
            document.addEventListener("visibilitychange", function() {
                if (s.requested && !s.lock && document.visibilityState == "visible") {
                    navigator.wakeLock.request("screen").then(function(lock) {
                        s.lock = lock;
                    }).catch(function() {});
                } });
        }
        return 1;
    }
    else
    {
        if (s.lock)
        {
            try { s.lock.release(); }
            catch(e) {}
            s.lock = null;
        }
        return 1;
    }
});

bool device_keep_screen_on(bool enabled)
{
    return js_hw_device_keep_screen_on(enabled ? 1 : 0) ? true : false;
}

bool device_is_screen_kept_on(void)
{
    return EM_ASM_INT({
        return (Module._hw_device && Module._hw_device.requested) ? 1 : 0;
    })
               ? true
               : false;
}

EM_JS(void, js_hw_clipboard_install, (void), {
    if (Module._hw_clipboard) return;
    var state = {text : null};
    Module._hw_clipboard = state;
    function remember(event) {
        var data = event.clipboardData;
        if (data && event.type == "paste") state.text = data.getData("text/plain");
        else { var selection = String(window.getSelection ? window.getSelection() : ""); if (selection) state.text = selection; }
    }
    document.addEventListener("paste", remember, true);
    document.addEventListener("copy", remember, true);
    document.addEventListener("cut", remember, true);
});

EM_JS(int, js_hw_clipboard_set, (const char *utf8), {
    var text = UTF8ToString(utf8);
    Module._hw_clipboard.text = text;
    function fallback() {
        try {
            var area = document.createElement("textarea");
            area.value = text;
            area.style.position = "fixed";
            area.style.opacity = "0";
            document.body.appendChild(area);
            area.select();
            document.execCommand("copy");
            document.body.removeChild(area);
        } catch (e) {}
    }
    if (navigator.clipboard && navigator.clipboard.writeText)
        navigator.clipboard.writeText(text).catch(fallback);
    else
        fallback();
    return 1;
});

EM_JS(char *, js_hw_clipboard_get, (void), {

    var state = Module._hw_clipboard;
    if (navigator.clipboard && navigator.clipboard.readText && !state.reading) {
        state.reading = true;
        navigator.clipboard.readText().then(function(text) { state.text = text; })
            .catch(function() {}).then(function() { state.reading = false; });
    }
    var text = state.text;
    if (text == null) return 0;
    return stringToNewUTF8(text);
});

bool device_set_clipboard_text(const char *utf8)
{
    js_hw_clipboard_install();
    return utf8 && js_hw_clipboard_set(utf8);
}

char *device_get_clipboard_text(void)
{
    js_hw_clipboard_install();
    return js_hw_clipboard_get();
}

EM_JS(int, js_hw_vibrate, (int ms), {
    if (!navigator.vibrate) return 0;
    try { return navigator.vibrate(ms) ? 1 : 0; } catch (e) { return 0; }
});

bool device_haptic(DeviceHaptic kind)
{
    static const int durations[DEVICE_HAPTIC_COUNT] = {8, 15, 25, 5, 20, 30, 40};
    return (int)kind >= 0 && kind < DEVICE_HAPTIC_COUNT && js_hw_vibrate(durations[kind]);
}

EM_JS(void, js_hw_preferences, (float *out), {
    function query(text) { return window.matchMedia && window.matchMedia(text).matches ? 1 : 0; }
    var ratio = window.devicePixelRatio || 1;
    var probe = Module._hw_safe_probe;
    if (!probe) {
        probe = document.createElement("div");
        probe.style.cssText = "position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;" +
            "padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)";
        document.body.appendChild(probe);
        Module._hw_safe_probe = probe;
    }
    var style = getComputedStyle(probe);
    var root = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
    var values = [
        query("(prefers-color-scheme: dark)"), query("(prefers-reduced-motion: reduce)"),
        query("(prefers-contrast: more)") || query("(forced-colors: active)"), root / 16,
        (parseFloat(style.paddingTop) || 0) * ratio, (parseFloat(style.paddingRight) || 0) * ratio,
        (parseFloat(style.paddingBottom) || 0) * ratio, (parseFloat(style.paddingLeft) || 0) * ratio,
        0
    ];

    var view = window.visualViewport;
    if (view && Math.abs(view.scale - 1) < 0.01)
        values[8] = Math.max(0, window.innerHeight - view.height - view.offsetTop) * ratio;
    for (var i = 0; i < values.length; i++) HEAPF32[(out >> 2) + i] = values[i];
});

void device_get_preferences(DevicePreferences *out)
{
    float values[9];
    if (!out)
        return;
    js_hw_preferences(values);
    out->dark_mode = values[0] != 0.0f;
    out->reduced_motion = values[1] != 0.0f;
    out->high_contrast = values[2] != 0.0f;
    out->font_scale = values[3] > 0.0f ? values[3] : 1.0f;
    out->safe_top = values[4];
    out->safe_right = values[5];
    out->safe_bottom = values[6];
    out->safe_left = values[7];
    out->keyboard_inset = values[8];
}

EM_JS(int, js_hw_set_cursor, (const char *name), {
    var el = document.getElementById("canvas");
    if (!el) return 0;
    var css = UTF8ToString(name);
    if (el.style.cursor != css) el.style.cursor = css;
    return 1;
});

bool device_set_cursor(DeviceCursor cursor)
{
    return (int)cursor >= 0 && cursor < DEVICE_CURSOR_COUNT && js_hw_set_cursor(device_cursor_name(cursor));
}