#ifndef BUDO_MIDI_SERVICE_H
#define BUDO_MIDI_SERVICE_H

#include "core/api_error.h"
#include "midi_wrapper.h"

int midi_service_open_input(MidiContext *context, int index,
                            MidiCallback callback, void *user_data,
                            ApiError *error);
int midi_service_open_output(MidiContext *context, int index, ApiError *error);
bool midi_service_send_message(MidiContext *context, int handle,
                               uint8_t status, uint8_t data1, uint8_t data2,
                               ApiError *error);
bool midi_service_send_raw(MidiContext *context, int handle,
                           const uint8_t *data, size_t length,
                           ApiError *error);

#endif