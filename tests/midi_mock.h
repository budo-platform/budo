#ifndef TESTS_MIDI_MOCK_H
#define TESTS_MIDI_MOCK_H

#include "midi/midi_wrapper.h"
#include "midi/rtpmidi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void midi_mock_reset(void);
MidiContext *midi_mock_context(int index);
int midi_mock_live_count(void);
int midi_mock_destroy_count(void);
bool midi_mock_emit(MidiContext *ctx, int handle, const MidiMessage *message);
bool midi_mock_emit_sysex(MidiContext *ctx, int handle,
                          const uint8_t *data, size_t length);
size_t midi_mock_last_raw_size(MidiContext *ctx);
uint8_t midi_mock_last_raw_byte(MidiContext *ctx, size_t index);

RtpMidiContext *rtpmidi_mock_context(int index);
int rtpmidi_mock_live_count(void);
int rtpmidi_mock_destroy_count(void);
bool rtpmidi_mock_emit(RtpMidiContext *ctx, int session,
                       const MidiMessage *message);
bool rtpmidi_mock_callback_attached(RtpMidiContext *ctx, int session);
const char *rtpmidi_mock_session_name(RtpMidiContext *ctx, int session);

#endif