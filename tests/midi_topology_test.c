#include "midi/midi_topology.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static MidiDeviceInfo device(int id, const char *name, bool input)
{
    MidiDeviceInfo info = {0};
    info.id = id;
    info.is_input = input;
    info.is_output = !input;
    snprintf(info.name, sizeof(info.name), "%s", name);
    return info;
}

int main(void)
{
    MidiDeviceInfo inputs[] = {device(1, "Keyboard", true), device(2, "Pads", true)};
    MidiDeviceInfo outputs[] = {device(3, "Synth", false)};
    uint64_t baseline = midi_topology_fingerprint(inputs, 2, outputs, 1);

    MidiDeviceInfo identical_inputs[2];
    MidiDeviceInfo identical_outputs[1];
    memcpy(identical_inputs, inputs, sizeof(inputs));
    memcpy(identical_outputs, outputs, sizeof(outputs));
    assert(midi_topology_fingerprint(identical_inputs, 2, identical_outputs, 1) == baseline);

    identical_inputs[0].id = 7;
    assert(midi_topology_fingerprint(identical_inputs, 2, identical_outputs, 1) != baseline);
    identical_inputs[0] = inputs[0];

    snprintf(identical_inputs[0].name, sizeof(identical_inputs[0].name), "Keyboard 2");
    assert(midi_topology_fingerprint(identical_inputs, 2, identical_outputs, 1) != baseline);
    identical_inputs[0] = inputs[0];

    MidiDeviceInfo reordered[] = {inputs[1], inputs[0]};
    assert(midi_topology_fingerprint(reordered, 2, outputs, 1) != baseline);
    assert(midi_topology_fingerprint(inputs, 1, outputs, 1) != baseline);
    assert(midi_topology_fingerprint(inputs, 2, NULL, 0) != baseline);

    MidiDeviceInfo same_device_as_output[] = {inputs[0]};
    assert(midi_topology_fingerprint(NULL, 0, same_device_as_output, 1) !=
           midi_topology_fingerprint(same_device_as_output, 1, NULL, 0));

    MidiTopologyObserver observer;
    midi_topology_observer_reset(&observer, baseline, 4);
    assert(observer.generation == 0);
    assert(!midi_topology_observer_update(&observer, baseline, 4));
    assert(midi_topology_observer_update(&observer, baseline, 5));
    assert(observer.generation == 1);
    assert(!midi_topology_observer_update(&observer, baseline, 5));
    assert(midi_topology_observer_update(&observer,
                                         midi_topology_fingerprint(reordered, 2, outputs, 1), 5));
    assert(observer.generation == 2);

    puts("MIDI topology fingerprint tests passed");
    return 0;
}