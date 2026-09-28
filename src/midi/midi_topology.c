#include "midi_topology.h"

#include <string.h>

static uint64_t fingerprint_bytes(uint64_t hash, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; i++)
    {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t midi_topology_fingerprint(const MidiDeviceInfo *inputs, size_t input_count,
                                   const MidiDeviceInfo *outputs, size_t output_count)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = fingerprint_bytes(hash, &input_count, sizeof(input_count));
    hash = fingerprint_bytes(hash, &output_count, sizeof(output_count));
    for (size_t i = 0; i < input_count; i++)
    {
        const uint8_t kind = 1;
        hash = fingerprint_bytes(hash, &kind, sizeof(kind));
        hash = fingerprint_bytes(hash, &inputs[i].id, sizeof(inputs[i].id));
        hash = fingerprint_bytes(hash, inputs[i].name, strlen(inputs[i].name) + 1);
    }
    for (size_t i = 0; i < output_count; i++)
    {
        const uint8_t kind = 2;
        hash = fingerprint_bytes(hash, &kind, sizeof(kind));
        hash = fingerprint_bytes(hash, &outputs[i].id, sizeof(outputs[i].id));
        hash = fingerprint_bytes(hash, outputs[i].name, strlen(outputs[i].name) + 1);
    }
    return hash;
}

void midi_topology_observer_reset(MidiTopologyObserver *observer,
                                  uint64_t fingerprint, uint64_t backend_generation)
{
    observer->fingerprint = fingerprint;
    observer->backend_generation = backend_generation;
    observer->generation = 0;
}

bool midi_topology_observer_update(MidiTopologyObserver *observer,
                                   uint64_t fingerprint, uint64_t backend_generation)
{
    if (observer->fingerprint == fingerprint &&
        observer->backend_generation == backend_generation)
        return false;
    observer->fingerprint = fingerprint;
    observer->backend_generation = backend_generation;
    observer->generation++;
    return true;
}