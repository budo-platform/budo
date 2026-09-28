#include "device/device_wrapper.h"

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