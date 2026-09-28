#ifndef MIDI_TOPOLOGY_H
#define MIDI_TOPOLOGY_H

#include "midi_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint64_t fingerprint;
        uint64_t backend_generation;
        uint32_t generation;
    } MidiTopologyObserver;

    uint64_t midi_topology_fingerprint(const MidiDeviceInfo *inputs, size_t input_count,
                                       const MidiDeviceInfo *outputs, size_t output_count);

    void midi_topology_observer_reset(MidiTopologyObserver *observer,
                                      uint64_t fingerprint, uint64_t backend_generation);

    bool midi_topology_observer_update(MidiTopologyObserver *observer,
                                       uint64_t fingerprint, uint64_t backend_generation);

#ifdef __cplusplus
}
#endif

#endif