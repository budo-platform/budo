#include "midi/midi_event_queue.h"
#include "midi/midi_stream_parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    MidiMessage messages[32];
    int message_count;
    uint8_t sysex[4][MIDI_SYSEX_MAX_SIZE];
    size_t sysex_length[4];
    int sysex_count;
    char order[64];
} Received;

static void on_message(const MidiMessage *message, void *user_data)
{
    Received *received = (Received *)user_data;
    assert(received->message_count < 32);
    received->messages[received->message_count++] = *message;
    strcat(received->order, "m");
}

static void on_sysex(const uint8_t *data, size_t length, uint64_t timestamp_us, void *user_data)
{
    Received *received = (Received *)user_data;
    (void)timestamp_us;
    assert(received->sysex_count < 4);
    memcpy(received->sysex[received->sysex_count], data, length);
    received->sysex_length[received->sysex_count++] = length;
    strcat(received->order, "s");
}

static MidiStreamParser *fresh(Received *received)
{
    static MidiStreamParser parser;
    memset(&parser, 0, sizeof(parser));
    midi_stream_parser_reset(&parser);
    memset(received, 0, sizeof(*received));
    return &parser;
}

static void feed(MidiStreamParser *parser, Received *received, const uint8_t *bytes, size_t length)
{
    midi_stream_parser_feed(parser, bytes, length, 1000, on_message, on_sysex, received);
}

static void test_sysex_across_chunks(void)
{
    static Received received;
    MidiStreamParser *parser = fresh(&received);
    
    const uint8_t a[] = {0xF0, 0x43, 0x10};
    const uint8_t b[] = {0x01, 0xF8, 0x02};
    const uint8_t c[] = {0x03, 0xF7, 0xB0, 0x07, 0x64};
    feed(parser, &received, a, sizeof(a));
    assert(received.sysex_count == 0);
    feed(parser, &received, b, sizeof(b));
    feed(parser, &received, c, sizeof(c));
    const uint8_t expected[] = {0xF0, 0x43, 0x10, 0x01, 0x02, 0x03, 0xF7};
    assert(received.sysex_count == 1);
    assert(received.sysex_length[0] == sizeof(expected));
    assert(memcmp(received.sysex[0], expected, sizeof(expected)) == 0);
    assert(received.message_count == 1 && received.messages[0].status == 0xB0 &&
           received.messages[0].data1 == 0x07 && received.messages[0].data2 == 0x64);
    assert(strcmp(received.order, "sm") == 0);
}

static void test_running_status_and_split_messages(void)
{
    static Received received;
    MidiStreamParser *parser = fresh(&received);
    
    const uint8_t a[] = {0x91, 0x3C};
    const uint8_t b[] = {0x64, 0x3E, 0x50, 0x40};
    const uint8_t c[] = {0x00, 0xC2, 0x05, 0x06};
    feed(parser, &received, a, sizeof(a));
    assert(received.message_count == 0);
    feed(parser, &received, b, sizeof(b));
    feed(parser, &received, c, sizeof(c));
    assert(received.message_count == 5);
    assert(received.messages[0].status == 0x91 && received.messages[0].data1 == 0x3C && received.messages[0].data2 == 0x64);
    assert(received.messages[1].status == 0x91 && received.messages[1].data1 == 0x3E && received.messages[1].data2 == 0x50);
    assert(received.messages[2].status == 0x91 && received.messages[2].data1 == 0x40 && received.messages[2].data2 == 0x00);
    
    assert(received.messages[3].status == 0xC2 && received.messages[3].data1 == 0x05 && received.messages[3].data2 == 0);
    assert(received.messages[4].status == 0xC2 && received.messages[4].data1 == 0x06);
}

static void test_dropped_sysex(void)
{
    static Received received;
    MidiStreamParser *parser = fresh(&received);
    
    uint8_t *big = (uint8_t *)malloc(MIDI_SYSEX_MAX_SIZE + 16);
    big[0] = 0xF0;
    memset(big + 1, 0x11, MIDI_SYSEX_MAX_SIZE + 14);
    big[MIDI_SYSEX_MAX_SIZE + 15] = 0xF7;
    feed(parser, &received, big, MIDI_SYSEX_MAX_SIZE + 16);
    free(big);
    assert(received.sysex_count == 0 && parser->dropped_sysex == 1);

    const uint8_t interrupted[] = {0xF0, 0x01, 0x02, 0x90, 0x3C, 0x64};
    feed(parser, &received, interrupted, sizeof(interrupted));
    assert(received.sysex_count == 0 && parser->dropped_sysex == 2);
    assert(received.message_count == 1 && received.messages[0].status == 0x90);

    uint8_t *max = (uint8_t *)malloc(MIDI_SYSEX_MAX_SIZE);
    max[0] = 0xF0;
    memset(max + 1, 0x22, MIDI_SYSEX_MAX_SIZE - 2);
    max[MIDI_SYSEX_MAX_SIZE - 1] = 0xF7;
    feed(parser, &received, max, MIDI_SYSEX_MAX_SIZE);
    free(max);
    assert(received.sysex_count == 1 && received.sysex_length[0] == MIDI_SYSEX_MAX_SIZE);
}

static void test_system_messages(void)
{
    static Received received;
    MidiStreamParser *parser = fresh(&received);

    const uint8_t bytes[] = {0x90, 0x3C, 0x64, 0xF2, 0x10, 0x20, 0x3E, 0x64, 0xF1, 0x05, 0xFE, 0xB0, 0x40, 0x7F};
    feed(parser, &received, bytes, sizeof(bytes));
    assert(received.message_count == 2);
    assert(received.messages[0].status == 0x90);
    assert(received.messages[1].status == 0xB0 && received.messages[1].data1 == 0x40 && received.messages[1].data2 == 0x7F);
}

static void test_event_queue_order(void)
{
    MidiEventQueue *queue = midi_event_queue_create(4, 2);
    assert(queue);
    MidiMessage bank = {0xB0, 0x00, 0x01, 0};
    MidiMessage program = {0xC0, 0x05, 0, 0};
    const uint8_t dump[] = {0xF0, 0x7E, 0xF7};
    assert(midi_event_queue_push_message(queue, 3, &bank));
    assert(midi_event_queue_push_sysex(queue, 3, dump, sizeof(dump), 0));
    assert(midi_event_queue_push_message(queue, 3, &program));
    assert(midi_event_queue_push_sysex(queue, 3, dump, sizeof(dump), 0));
    
    assert(!midi_event_queue_push_message(queue, 3, &program));
    assert(!midi_event_queue_push_sysex(queue, 3, dump, sizeof(dump), 0));
    assert(midi_event_queue_dropped_messages(queue) == 1);
    assert(midi_event_queue_dropped_sysex(queue) == 1);

    static uint8_t buffer[MIDI_SYSEX_MAX_SIZE];
    MidiEvent event;
    assert(midi_event_queue_pop(queue, &event, buffer) && !event.is_sysex && event.message.status == 0xB0 && event.handle == 3);
    assert(midi_event_queue_pop(queue, &event, buffer) && event.is_sysex && event.sysex_length == 3 && buffer[1] == 0x7E);
    assert(midi_event_queue_pop(queue, &event, buffer) && !event.is_sysex && event.message.status == 0xC0);
    assert(midi_event_queue_pop(queue, &event, buffer) && event.is_sysex);
    assert(!midi_event_queue_pop(queue, &event, buffer));
    midi_event_queue_destroy(queue);
}

int main(void)
{
    test_sysex_across_chunks();
    test_running_status_and_split_messages();
    test_dropped_sysex();
    test_system_messages();
    test_event_queue_order();
    puts("midi_stream_parser_test: ok");
    return 0;
}