#include "midi/midi_stream_parser.h"

#include <string.h>

void midi_stream_parser_reset(MidiStreamParser *parser)
{
    if (!parser)
        return;
    size_t dropped = parser->dropped_sysex;
    parser->status = 0;
    parser->data_count = 0;
    parser->data_expected = 0;
    parser->system_skip = 0;
    parser->in_sysex = false;
    parser->sysex_overflow = false;
    parser->sysex_length = 0;
    parser->sysex_timestamp = 0;
    parser->message_timestamp = 0;
    parser->dropped_sysex = dropped;
}

static int channel_data_length(uint8_t status)
{
    uint8_t type = status & 0xF0;
    return type == 0xC0 || type == 0xD0 ? 1 : 2;
}

static int system_data_length(uint8_t status)
{
    switch (status)
    {
    case 0xF1: 
    case 0xF3: 
        return 1;
    case 0xF2: 
        return 2;
    default: 
        return 0;
    }
}

static void abandon_sysex(MidiStreamParser *parser)
{
    if (parser->in_sysex)
        parser->dropped_sysex++;
    parser->in_sysex = false;
    parser->sysex_overflow = false;
    parser->sysex_length = 0;
}

void midi_stream_parser_feed(MidiStreamParser *parser, const uint8_t *bytes, size_t length,
                             uint64_t timestamp_us, MidiStreamMessageFn on_message,
                             MidiStreamSysExFn on_sysex, void *user_data)
{
    if (!parser || !bytes)
        return;
    for (size_t index = 0; index < length; index++)
    {
        uint8_t byte = bytes[index];

        if (byte >= 0xF8)
            continue;

        if (byte == 0xF0)
        {
            abandon_sysex(parser);
            parser->status = 0; 
            parser->data_count = 0;
            parser->system_skip = 0;
            parser->in_sysex = true;
            parser->sysex_timestamp = timestamp_us;
            parser->sysex[0] = 0xF0;
            parser->sysex_length = 1;
            continue;
        }

        if (byte == 0xF7)
        {
            if (parser->in_sysex)
            {
                if (parser->sysex_overflow || parser->sysex_length >= MIDI_SYSEX_MAX_SIZE)
                {
                    parser->dropped_sysex++;
                }
                else
                {
                    parser->sysex[parser->sysex_length++] = 0xF7;
                    if (on_sysex)
                        on_sysex(parser->sysex, parser->sysex_length, parser->sysex_timestamp, user_data);
                }
            }
            parser->in_sysex = false;
            parser->sysex_overflow = false;
            parser->sysex_length = 0;
            continue;
        }

        if (byte >= 0xF1)
        {
            
            abandon_sysex(parser);
            parser->status = 0;
            parser->data_count = 0;
            parser->system_skip = system_data_length(byte);
            continue;
        }

        if (byte >= 0x80)
        {
            abandon_sysex(parser);
            parser->status = byte;
            parser->data_count = 0;
            parser->data_expected = channel_data_length(byte);
            parser->system_skip = 0;
            parser->message_timestamp = timestamp_us;
            continue;
        }

        if (parser->in_sysex)
        {
            if (parser->sysex_length < MIDI_SYSEX_MAX_SIZE - 1)
                parser->sysex[parser->sysex_length++] = byte;
            else
                parser->sysex_overflow = true;
            continue;
        }
        if (parser->system_skip > 0)
        {
            parser->system_skip--;
            continue;
        }
        if (parser->status == 0)
            continue; 
        if (parser->data_count == 0)
            parser->message_timestamp = timestamp_us;
        parser->data[parser->data_count++] = byte;
        if (parser->data_count < parser->data_expected)
            continue;

        MidiMessage message;
        memset(&message, 0, sizeof(message));
        message.status = parser->status;
        message.data1 = parser->data[0];
        message.data2 = parser->data_expected > 1 ? parser->data[1] : 0;
        message.timestamp = parser->message_timestamp;
        parser->data_count = 0; 
        if (on_message)
            on_message(&message, user_data);
    }
}