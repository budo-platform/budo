#include "midi_service.h"

static bool midi_service_context(MidiContext *context, ApiError *error)
{
    api_error_clear(error);
    if (context)
        return true;
    api_error_set(error, API_STATUS_UNSUPPORTED,
                  "midi.unavailable", "MIDI is unavailable");
    return false;
}

static void midi_service_wrapper_error(MidiContext *context,
                                       const char *code, ApiError *error)
{
    const char *message = midi_get_error(context);
    api_error_set(error, API_STATUS_APPLICATION_ERROR, code,
                  message && message[0] ? message : "MIDI operation failed");
}

int midi_service_open_input(MidiContext *context, int index,
                            MidiCallback callback, void *user_data,
                            ApiError *error)
{
    int handle;
    if (!midi_service_context(context, error))
        return -1;
    if (index < 0 || !callback)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "midi.invalid_input", "Input index and callback are required");
        return -1;
    }
    handle = midi_open_input(context, index, callback, user_data);
    if (handle < 0)
        midi_service_wrapper_error(context, "midi.open_input_failed", error);
    return handle;
}

int midi_service_open_output(MidiContext *context, int index, ApiError *error)
{
    int handle;
    if (!midi_service_context(context, error))
        return -1;
    if (index < 0)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "midi.invalid_output", "Output index is invalid");
        return -1;
    }
    handle = midi_open_output(context, index);
    if (handle < 0)
        midi_service_wrapper_error(context, "midi.open_output_failed", error);
    return handle;
}

bool midi_service_send_message(MidiContext *context, int handle,
                               uint8_t status, uint8_t data1, uint8_t data2,
                               ApiError *error)
{
    if (!midi_service_context(context, error))
        return false;
    if (handle < 0 || status < 0x80)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "midi.invalid_message", "MIDI message is invalid");
        return false;
    }
    if (midi_send_message(context, handle, status, data1, data2))
        return true;
    midi_service_wrapper_error(context, "midi.send_failed", error);
    return false;
}

bool midi_service_send_raw(MidiContext *context, int handle,
                           const uint8_t *data, size_t length,
                           ApiError *error)
{
    if (!midi_service_context(context, error))
        return false;
    if (handle < 0 || !data || length == 0 || length > MIDI_SYSEX_MAX_SIZE)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "midi.invalid_message", "Raw MIDI message is invalid");
        return false;
    }
    if (midi_send_raw(context, handle, data, length))
        return true;
    midi_service_wrapper_error(context, "midi.send_failed", error);
    return false;
}