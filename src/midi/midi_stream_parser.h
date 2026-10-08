#ifndef BUDO_MIDI_STREAM_PARSER_H
#define BUDO_MIDI_STREAM_PARSER_H

#include "midi/midi_wrapper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*MidiStreamMessageFn)(const MidiMessage *message, void *user_data);
    typedef void (*MidiStreamSysExFn)(const uint8_t *data, size_t length, uint64_t timestamp_us, void *user_data);

    typedef struct
    {
        uint8_t status;          
        uint8_t data[2];
        int data_count;
        int data_expected;
        int system_skip;         
        bool in_sysex;
        bool sysex_overflow;
        size_t sysex_length;
        uint64_t sysex_timestamp;
        uint64_t message_timestamp;
        size_t dropped_sysex;    
        uint8_t sysex[MIDI_SYSEX_MAX_SIZE];
    } MidiStreamParser;

    void midi_stream_parser_reset(MidiStreamParser *parser);

    void midi_stream_parser_feed(MidiStreamParser *parser, const uint8_t *bytes, size_t length,
                                 uint64_t timestamp_us, MidiStreamMessageFn on_message,
                                 MidiStreamSysExFn on_sysex, void *user_data);

#ifdef __cplusplus
}
#endif

#endif