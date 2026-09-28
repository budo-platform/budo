#include "capabilities.h"

#ifdef BUDO_NEURAL
#include "neural/neural_wrapper.h"
#endif

#ifdef BUDO_WEB
#include <emscripten.h>
#endif

bool capabilities_get_snapshot(CapabilitySnapshot *snapshot, ApiError *error)
{
    api_error_clear(error);
    if (!snapshot)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "capabilities.invalid_snapshot",
                      "Capability snapshot output is required");
        return false;
    }

    snapshot->neural = capability_neural_available();
    snapshot->llamacpp = capability_llamacpp_available();
    snapshot->midi = capability_midi_available();
    snapshot->udp = capability_udp_available();
    snapshot->http = capability_http_available();
    snapshot->sensors = capability_sensors_available();
    return true;
}

bool capability_neural_available(void)
{
#ifdef BUDO_NEURAL
    return neural_is_available();
#else
    return false;
#endif
}

bool capability_llamacpp_available(void)
{
#ifdef BUDO_LLAMACPP
    return true;
#else
    return false;
#endif
}

bool capability_midi_available(void)
{

    return true;
}

bool capability_udp_available(void)
{
    
    return BUDO_CAPABILITY_UDP_AVAILABLE;
}

bool capability_http_available(void)
{

    return true;
}

#ifdef BUDO_WEB
EM_JS(int, hw_cap_sensors_present, (void), {
    return (("DeviceMotionEvent" in window) ||
            ("DeviceOrientationEvent" in window))
               ? 1
               : 0;
});
#endif

bool capability_sensors_available(void)
{
#if defined(BUDO_ANDROID)
    return true;
#elif defined(BUDO_WEB)
    static int cached = -1;
    if (cached < 0)
        cached = hw_cap_sensors_present();
    return cached != 0;
#else
    return false;
#endif
}