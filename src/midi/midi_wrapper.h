#ifndef MIDI_WRAPPER_H
#define MIDI_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MIDI_MAX_DEVICES 32
#define MIDI_MAX_DEVICE_NAME 256

    typedef enum
    {
        MIDI_NOTE_OFF = 0x80,
        MIDI_NOTE_ON = 0x90,
        MIDI_POLY_PRESSURE = 0xA0,
        MIDI_CONTROL_CHANGE = 0xB0,
        MIDI_PROGRAM_CHANGE = 0xC0,
        MIDI_CHANNEL_PRESSURE = 0xD0,
        MIDI_PITCH_BEND = 0xE0,
        MIDI_SYSTEM = 0xF0
    } MidiMessageType;

    typedef enum
    {
        MIDI_CC_MOD_WHEEL = 1,
        MIDI_CC_BREATH = 2,
        MIDI_CC_FOOT = 4,
        MIDI_CC_VOLUME = 7,
        MIDI_CC_BALANCE = 8,
        MIDI_CC_PAN = 10,
        MIDI_CC_EXPRESSION = 11,
        MIDI_CC_SUSTAIN = 64,
        MIDI_CC_PORTAMENTO = 65,
        MIDI_CC_SOSTENUTO = 66,
        MIDI_CC_SOFT = 67,
        MIDI_CC_ALL_SOUND_OFF = 120,
        MIDI_CC_ALL_NOTES_OFF = 123
    } MidiControlChange;

    typedef struct MidiContext MidiContext;

#define MIDI_SYSEX_MAX_SIZE 65535

    typedef struct
    {
        uint8_t status;     
        uint8_t data1;      
        uint8_t data2;      
        uint64_t timestamp; 
    } MidiMessage;

    typedef struct
    {
        int id;
        char name[MIDI_MAX_DEVICE_NAME];
        bool is_input;
        bool is_output;
    } MidiDeviceInfo;

    typedef void (*MidiCallback)(int device_id, const MidiMessage *message, void *user_data);

    typedef void (*MidiSysExCallback)(int device_id, const uint8_t *data, size_t length, void *user_data);

    MidiContext *midi_create(void);

    void midi_destroy(MidiContext *ctx);

    int midi_get_input_count(MidiContext *ctx);

    int midi_get_output_count(MidiContext *ctx);

    bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info);

    bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info);

    void midi_refresh_devices(MidiContext *ctx);

    uint64_t midi_get_device_generation(MidiContext *ctx);

    uint64_t midi_get_device_topology_fingerprint(MidiContext *ctx);

    int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data);

    void midi_close_input(MidiContext *ctx, int handle);

    bool midi_set_sysex_callback(MidiContext *ctx, int handle, MidiSysExCallback callback, void *user_data);

    int midi_open_output(MidiContext *ctx, int index);

    void midi_close_output(MidiContext *ctx, int handle);

    bool midi_send_message(MidiContext *ctx, int handle, uint8_t status, uint8_t data1, uint8_t data2);

    bool midi_send_raw(MidiContext *ctx, int handle, const uint8_t *data, size_t length);

    bool midi_note_on(MidiContext *ctx, int handle, int channel, int note, int velocity);

    bool midi_note_off(MidiContext *ctx, int handle, int channel, int note, int velocity);

    bool midi_control_change(MidiContext *ctx, int handle, int channel, int control, int value);

    bool midi_program_change(MidiContext *ctx, int handle, int channel, int program);

    bool midi_pitch_bend(MidiContext *ctx, int handle, int channel, int value);

    bool midi_channel_pressure(MidiContext *ctx, int handle, int channel, int pressure);

    bool midi_poly_pressure(MidiContext *ctx, int handle, int channel, int note, int pressure);

    MidiMessageType midi_get_type(uint8_t status);

    int midi_get_channel(uint8_t status);

    const char *midi_get_error(MidiContext *ctx);

    typedef struct RtpMidiContext RtpMidiContext;

#ifdef __cplusplus
}
#endif

#endif