#ifndef BUDO_CAPABILITIES_H
#define BUDO_CAPABILITIES_H

#include "api_error.h"

#include <stdbool.h>

#ifdef BUDO_WEB
#define BUDO_CAPABILITY_UDP_AVAILABLE false
#else
#define BUDO_CAPABILITY_UDP_AVAILABLE true
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct CapabilitySnapshot
    {
        bool neural;
        bool llamacpp;
        bool midi;
        bool udp;
        bool http;
        bool sensors;
    } CapabilitySnapshot;

    bool capabilities_get_snapshot(CapabilitySnapshot *snapshot,
                                   ApiError *error);

    bool capability_neural_available(void);

    bool capability_llamacpp_available(void);

    bool capability_midi_available(void);

    bool capability_udp_available(void);

    bool capability_http_available(void);

    bool capability_sensors_available(void);

#ifdef __cplusplus
}
#endif

#endif