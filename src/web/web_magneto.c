#include "magneto/magneto_wrapper.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MagnetoContext
{
    bool active;
};

EM_JS(int, js_hw_magneto_has_motion, (void), {
    return ("DeviceMotionEvent" in window) ? 1 : 0;
});

EM_JS(int, js_hw_magneto_has_orientation, (void), {
    return ("DeviceOrientationEvent" in window) ? 1 : 0;
});

EM_JS(int, js_hw_magneto_start, (void), {
    if (Module._hw_magneto && Module._hw_magneto.listening)
        return 1;

    if (!Module._hw_magneto) {
        Module._hw_magneto = {
            listening: false,
            hasAccel: false,
            hasCompass: false,
            accel: { x: 0, y: 0, z: 0 },
            compass: { alpha: 0, beta: 0, gamma: 0, heading: 0 },
            motionHandler: null,
            orientHandler: null
        };
    }

    var state = Module._hw_magneto;

    state.motionHandler = function(e) {
        var a = e.accelerationIncludingGravity;
        if (a && a.x != null) {
            state.hasAccel = true;
            state.accel.x = a.x || 0;
            state.accel.y = a.y || 0;
            state.accel.z = a.z || 0;
        }
    };

    state.orientHandler = function(e) {
        if (e.alpha != null) {
            state.hasCompass = true;
            state.compass.alpha = e.alpha || 0;
            state.compass.beta = e.beta || 0;
            state.compass.gamma = e.gamma || 0;

            state.compass.heading = (360 - (e.alpha || 0)) % 360;
        }
    };

    var needsPermission = false;

    if (typeof DeviceMotionEvent.requestPermission == "function") {
        needsPermission = true;
        DeviceMotionEvent.requestPermission().then(function(result) {
            if (result == "granted") {
                window.addEventListener("devicemotion", state.motionHandler);
            }
        }).catch(function() {
            
            var handler = function() {
                DeviceMotionEvent.requestPermission().then(function(result) {
                    if (result == "granted") {
                        window.addEventListener("devicemotion", state.motionHandler);
                    }
                }).catch(function(){});
                document.removeEventListener("touchstart", handler);
                document.removeEventListener("click", handler);
            };
            document.addEventListener("touchstart", handler, { once: true });
            document.addEventListener("click", handler, { once: true });
        });
    } else {
        window.addEventListener("devicemotion", state.motionHandler);
    }

    if (typeof DeviceOrientationEvent.requestPermission == "function") {
        DeviceOrientationEvent.requestPermission().then(function(result) {
            if (result == "granted") {
                window.addEventListener("deviceorientation", state.orientHandler);
            }
        }).catch(function() {
            var handler = function() {
                DeviceOrientationEvent.requestPermission().then(function(result) {
                    if (result == "granted") {
                        window.addEventListener("deviceorientation", state.orientHandler);
                    }
                }).catch(function(){});
                document.removeEventListener("touchstart", handler);
                document.removeEventListener("click", handler);
            };
            document.addEventListener("touchstart", handler, { once: true });
            document.addEventListener("click", handler, { once: true });
        });
    } else {
        window.addEventListener("deviceorientation", state.orientHandler);
    }

    state.listening = true;
    return 1;
});

EM_JS(void, js_hw_magneto_stop, (void), {
    if (!Module._hw_magneto || !Module._hw_magneto.listening)
        return;

    var state = Module._hw_magneto;

    if (state.motionHandler) {
        window.removeEventListener("devicemotion", state.motionHandler);
        state.motionHandler = null;
    }
    if (state.orientHandler) {
        window.removeEventListener("deviceorientation", state.orientHandler);
        state.orientHandler = null;
    }

    state.listening = false;
    state.hasAccel = false;
    state.hasCompass = false;
});

EM_JS(int, js_hw_magneto_has_accel_data, (void), {
    return (Module._hw_magneto && Module._hw_magneto.hasAccel) ? 1 : 0;
});

EM_JS(float, js_hw_magneto_get_accel_x, (void), {
    return Module._hw_magneto ? Module._hw_magneto.accel.x : 0;
});

EM_JS(float, js_hw_magneto_get_accel_y, (void), {
    return Module._hw_magneto ? Module._hw_magneto.accel.y : 0;
});

EM_JS(float, js_hw_magneto_get_accel_z, (void), {
    return Module._hw_magneto ? Module._hw_magneto.accel.z : 0;
});

EM_JS(int, js_hw_magneto_has_compass_data, (void), {
    return (Module._hw_magneto && Module._hw_magneto.hasCompass) ? 1 : 0;
});

EM_JS(float, js_hw_magneto_get_compass_x, (void), {
    
    return Module._hw_magneto ? Module._hw_magneto.compass.beta : 0;
});

EM_JS(float, js_hw_magneto_get_compass_y, (void), {
    
    return Module._hw_magneto ? Module._hw_magneto.compass.gamma : 0;
});

EM_JS(float, js_hw_magneto_get_compass_z, (void), {
    
    return Module._hw_magneto ? Module._hw_magneto.compass.alpha : 0;
});

EM_JS(float, js_hw_magneto_get_heading, (void), {
    return Module._hw_magneto ? Module._hw_magneto.compass.heading : 0;
});

MagnetoContext *magneto_create(void)
{
    MagnetoContext *ctx = calloc(1, sizeof(MagnetoContext));
    return ctx;
}

void magneto_destroy(MagnetoContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->active)
        magneto_stop(ctx);
    free(ctx);
}

bool magneto_is_available(MagnetoContext *ctx)
{
    (void)ctx;
    return js_hw_magneto_has_motion() || js_hw_magneto_has_orientation();
}

bool magneto_has_accelerometer(MagnetoContext *ctx)
{
    (void)ctx;
    return js_hw_magneto_has_motion();
}

bool magneto_has_compass(MagnetoContext *ctx)
{
    (void)ctx;
    return js_hw_magneto_has_orientation();
}

bool magneto_start(MagnetoContext *ctx)
{
    if (!ctx)
        return false;
    if (ctx->active)
        return true;
    if (!magneto_is_available(ctx))
        return false;

    if (js_hw_magneto_start())
    {
        ctx->active = true;
        return true;
    }
    return false;
}

void magneto_stop(MagnetoContext *ctx)
{
    if (!ctx || !ctx->active)
        return;
    js_hw_magneto_stop();
    ctx->active = false;
}

bool magneto_is_active(MagnetoContext *ctx)
{
    if (!ctx)
        return false;
    return ctx->active;
}

bool magneto_get_accel(MagnetoContext *ctx, MagnetoAccelData *data)
{
    if (!ctx || !ctx->active || !data)
        return false;

    if (!js_hw_magneto_has_accel_data())
    {
        memset(data, 0, sizeof(*data));
        return false;
    }

    data->x = js_hw_magneto_get_accel_x();
    data->y = js_hw_magneto_get_accel_y();
    data->z = js_hw_magneto_get_accel_z();
    return true;
}

bool magneto_get_compass(MagnetoContext *ctx, MagnetoCompassData *data)
{
    if (!ctx || !ctx->active || !data)
        return false;

    if (!js_hw_magneto_has_compass_data())
    {
        memset(data, 0, sizeof(*data));
        return false;
    }

    data->x = js_hw_magneto_get_compass_x();
    data->y = js_hw_magneto_get_compass_y();
    data->z = js_hw_magneto_get_compass_z();
    data->heading = js_hw_magneto_get_heading();
    return true;
}