#ifndef BUDO_MIDI_EVENT_QUEUE_H
#define BUDO_MIDI_EVENT_QUEUE_H

#include "midi/midi_wrapper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct MidiEventQueue MidiEventQueue;

    typedef struct
    {
        int handle;          
        bool is_sysex;
        MidiMessage message; 
        size_t sysex_length; 
    } MidiEvent;

    MidiEventQueue *midi_event_queue_create(size_t message_capacity, size_t sysex_capacity);
    void midi_event_queue_destroy(MidiEventQueue *queue);

    bool midi_event_queue_push_message(MidiEventQueue *queue, int handle, const MidiMessage *message);
    bool midi_event_queue_push_sysex(MidiEventQueue *queue, int handle, const uint8_t *data, size_t length,
                                     uint64_t timestamp_us);

    bool midi_event_queue_pop(MidiEventQueue *queue, MidiEvent *event, uint8_t *sysex_buffer);

    size_t midi_event_queue_dropped_messages(MidiEventQueue *queue);
    size_t midi_event_queue_dropped_sysex(MidiEventQueue *queue);

#ifdef __cplusplus
}
#endif

#endif