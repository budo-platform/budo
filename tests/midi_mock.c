#include "tests/midi_mock.h"
#include "tests/udp_mock.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIDI_MOCK_MAX_CONTEXTS 16

struct MidiContext
{
    int id;
    bool alive;
    bool input_active[MIDI_MAX_DEVICES];
    MidiCallback callbacks[MIDI_MAX_DEVICES];
    void *callback_data[MIDI_MAX_DEVICES];
    MidiSysExCallback sysex_callbacks[MIDI_MAX_DEVICES];
    void *sysex_callback_data[MIDI_MAX_DEVICES];
    uint8_t last_raw[MIDI_SYSEX_MAX_SIZE];
    size_t last_raw_size;
    char error[128];
};

struct RtpMidiContext
{
    int id;
    bool alive;
    UdpContext *udp;
    bool session_active[RTPMIDI_MAX_SESSIONS];
    char session_names[RTPMIDI_MAX_SESSIONS][RTPMIDI_MAX_NAME];
    int session_ports[RTPMIDI_MAX_SESSIONS];
    RtpMidiCallback callbacks[RTPMIDI_MAX_SESSIONS];
    void *callback_data[RTPMIDI_MAX_SESSIONS];
    char error[128];
};

static MidiContext *midi_contexts[MIDI_MOCK_MAX_CONTEXTS];
static RtpMidiContext *rtpmidi_contexts[MIDI_MOCK_MAX_CONTEXTS];
static int midi_context_count;
static int midi_live_count;
static int midi_destroy_count_value;
static int rtpmidi_context_count;
static int rtpmidi_live_count_value;
static int rtpmidi_destroy_count_value;

void midi_mock_reset(void)
{
    assert(midi_live_count == 0);
    assert(rtpmidi_live_count_value == 0);
    for (int index = 0; index < midi_context_count; index++)
        free(midi_contexts[index]);
    for (int index = 0; index < rtpmidi_context_count; index++)
        free(rtpmidi_contexts[index]);
    memset(midi_contexts, 0, sizeof(midi_contexts));
    memset(rtpmidi_contexts, 0, sizeof(rtpmidi_contexts));
    midi_context_count = 0;
    midi_destroy_count_value = 0;
    rtpmidi_context_count = 0;
    rtpmidi_destroy_count_value = 0;
}

MidiContext *midi_mock_context(int index)
{
    return index >= 0 && index < midi_context_count
               ? midi_contexts[index]
               : NULL;
}

int midi_mock_live_count(void)
{
    return midi_live_count;
}

int midi_mock_destroy_count(void)
{
    return midi_destroy_count_value;
}

bool midi_mock_emit(MidiContext *ctx, int handle,
                    const MidiMessage *message)
{
    if (!ctx || !ctx->alive || !message || handle < 0 ||
        handle >= MIDI_MAX_DEVICES || !ctx->input_active[handle] ||
        !ctx->callbacks[handle])
        return false;
    ctx->callbacks[handle](handle, message, ctx->callback_data[handle]);
    return true;
}

bool midi_mock_emit_sysex(MidiContext *ctx, int handle,
                          const uint8_t *data, size_t length)
{
    if (!ctx || !ctx->alive || !data || handle < 0 ||
        handle >= MIDI_MAX_DEVICES || !ctx->input_active[handle] ||
        !ctx->sysex_callbacks[handle])
        return false;
    ctx->sysex_callbacks[handle](handle, data, length,
                                 ctx->sysex_callback_data[handle]);
    return true;
}

size_t midi_mock_last_raw_size(MidiContext *ctx)
{
    return ctx ? ctx->last_raw_size : 0;
}

uint8_t midi_mock_last_raw_byte(MidiContext *ctx, size_t index)
{
    return ctx && index < ctx->last_raw_size ? ctx->last_raw[index] : 0;
}

MidiContext *midi_create(void)
{
    MidiContext *ctx;
    if (midi_context_count >= MIDI_MOCK_MAX_CONTEXTS)
        return NULL;
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->id = midi_context_count + 1;
    ctx->alive = true;
    midi_contexts[midi_context_count++] = ctx;
    midi_live_count++;
    return ctx;
}

void midi_destroy(MidiContext *ctx)
{
    if (!ctx || !ctx->alive)
        return;
    ctx->alive = false;
    memset(ctx->input_active, 0, sizeof(ctx->input_active));
    memset(ctx->callbacks, 0, sizeof(ctx->callbacks));
    memset(ctx->callback_data, 0, sizeof(ctx->callback_data));
    memset(ctx->sysex_callbacks, 0, sizeof(ctx->sysex_callbacks));
    memset(ctx->sysex_callback_data, 0, sizeof(ctx->sysex_callback_data));
    midi_live_count--;
    midi_destroy_count_value++;
}

int midi_get_input_count(MidiContext *ctx)
{
    return ctx && ctx->alive ? 1 : 0;
}

int midi_get_output_count(MidiContext *ctx)
{
    return ctx && ctx->alive ? ctx->id : 0;
}

static bool midi_get_info(MidiContext *ctx, int index, MidiDeviceInfo *info,
                          bool input)
{
    if (!ctx || !ctx->alive || index != 0 || !info)
        return false;
    memset(info, 0, sizeof(*info));
    info->id = ctx->id;
    snprintf(info->name, sizeof(info->name), "mock-midi-%d", ctx->id);
    info->is_input = input;
    info->is_output = !input;
    return true;
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    return midi_get_info(ctx, index, info, true);
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    return midi_get_info(ctx, index, info, false);
}

void midi_refresh_devices(MidiContext *ctx)
{
    (void)ctx;
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    return ctx && ctx->alive ? (uint64_t)ctx->id : 0;
}

uint64_t midi_get_device_topology_fingerprint(MidiContext *ctx)
{
    return ctx && ctx->alive ? (uint64_t)(ctx->id * 17) : 0;
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback,
                    void *user_data)
{
    if (!ctx || !ctx->alive || index != 0 || !callback)
        return -1;
    ctx->input_active[0] = true;
    ctx->callbacks[0] = callback;
    ctx->callback_data[0] = user_data;
    return 0;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;
    ctx->input_active[handle] = false;
    ctx->callbacks[handle] = NULL;
    ctx->callback_data[handle] = NULL;
    ctx->sysex_callbacks[handle] = NULL;
    ctx->sysex_callback_data[handle] = NULL;
}

bool midi_set_sysex_callback(MidiContext *ctx, int handle,
                             MidiSysExCallback callback, void *user_data)
{
    if (!ctx || !ctx->alive || handle < 0 ||
        handle >= MIDI_MAX_DEVICES || !ctx->input_active[handle])
        return false;
    ctx->sysex_callbacks[handle] = callback;
    ctx->sysex_callback_data[handle] = user_data;
    return true;
}

int midi_open_output(MidiContext *ctx, int index)
{
    return ctx && ctx->alive && index == 0 ? ctx->id * 100 : -1;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    (void)ctx;
    (void)handle;
}

bool midi_send_raw(MidiContext *ctx, int handle,
                   const uint8_t *data, size_t length)
{
    (void)handle;
    if (!ctx || !ctx->alive || !data || length == 0 ||
        length > sizeof(ctx->last_raw))
        return false;
    memcpy(ctx->last_raw, data, length);
    ctx->last_raw_size = length;
    return true;
}

bool midi_send_message(MidiContext *ctx, int handle, uint8_t status,
                       uint8_t data1, uint8_t data2)
{
    const uint8_t data[] = {status, data1, data2};
    return midi_send_raw(ctx, handle, data, sizeof(data));
}

bool midi_note_on(MidiContext *ctx, int handle, int channel, int note,
                  int velocity)
{
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_NOTE_ON | (channel & 0x0f)),
                             (uint8_t)note, (uint8_t)velocity);
}

bool midi_note_off(MidiContext *ctx, int handle, int channel, int note,
                   int velocity)
{
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_NOTE_OFF | (channel & 0x0f)),
                             (uint8_t)note, (uint8_t)velocity);
}

bool midi_control_change(MidiContext *ctx, int handle, int channel,
                         int control, int value)
{
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_CONTROL_CHANGE | (channel & 0x0f)),
                             (uint8_t)control, (uint8_t)value);
}

bool midi_program_change(MidiContext *ctx, int handle, int channel, int program)
{
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_PROGRAM_CHANGE | (channel & 0x0f)),
                             (uint8_t)program, 0);
}

bool midi_pitch_bend(MidiContext *ctx, int handle, int channel, int value)
{
    int adjusted = value + 8192;
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_PITCH_BEND | (channel & 0x0f)),
                             (uint8_t)(adjusted & 0x7f),
                             (uint8_t)((adjusted >> 7) & 0x7f));
}

bool midi_channel_pressure(MidiContext *ctx, int handle, int channel,
                           int pressure)
{
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_CHANNEL_PRESSURE | (channel & 0x0f)),
                             (uint8_t)pressure, 0);
}

bool midi_poly_pressure(MidiContext *ctx, int handle, int channel, int note,
                        int pressure)
{
    return midi_send_message(ctx, handle,
                             (uint8_t)(MIDI_POLY_PRESSURE | (channel & 0x0f)),
                             (uint8_t)note, (uint8_t)pressure);
}

MidiMessageType midi_get_type(uint8_t status)
{
    return (MidiMessageType)(status & 0xf0);
}

int midi_get_channel(uint8_t status)
{
    return status < 0xf0 ? status & 0x0f : -1;
}

const char *midi_get_error(MidiContext *ctx)
{
    return ctx ? ctx->error : "Invalid MIDI context";
}

RtpMidiContext *rtpmidi_create(UdpContext *udp_ctx)
{
    RtpMidiContext *ctx;
    if (!udp_ctx || rtpmidi_context_count >= MIDI_MOCK_MAX_CONTEXTS)
        return NULL;
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->id = rtpmidi_context_count + 1;
    ctx->alive = true;
    ctx->udp = udp_ctx;
    rtpmidi_contexts[rtpmidi_context_count++] = ctx;
    rtpmidi_live_count_value++;
    return ctx;
}

void rtpmidi_destroy(RtpMidiContext *ctx)
{
    if (!ctx || !ctx->alive)
        return;
    assert(udp_mock_is_live(ctx->udp));
    for (int session = 0; session < RTPMIDI_MAX_SESSIONS; session++)
        assert(ctx->callbacks[session] == NULL);
    ctx->alive = false;
    rtpmidi_live_count_value--;
    rtpmidi_destroy_count_value++;
}

int rtpmidi_create_session(RtpMidiContext *ctx, const char *name,
                           int control_port)
{
    if (!ctx || !ctx->alive)
        return -1;
    for (int session = 0; session < RTPMIDI_MAX_SESSIONS; session++)
    {
        if (ctx->session_active[session])
            continue;
        ctx->session_active[session] = true;
        snprintf(ctx->session_names[session], RTPMIDI_MAX_NAME, "%s",
                 name ? name : "Budo");
        ctx->session_ports[session] = control_port == 0
                                          ? RTPMIDI_DEFAULT_PORT + ctx->id * 2
                                          : control_port;
        return session;
    }
    return -1;
}

bool rtpmidi_connect(RtpMidiContext *ctx, int session, const char *host,
                     int port)
{
    return ctx && ctx->alive && session >= 0 &&
           session < RTPMIDI_MAX_SESSIONS && ctx->session_active[session] &&
           host && port > 0;
}

void rtpmidi_destroy_session(RtpMidiContext *ctx, int session)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return;
    ctx->session_active[session] = false;
    ctx->callbacks[session] = NULL;
    ctx->callback_data[session] = NULL;
}

void rtpmidi_set_callback(RtpMidiContext *ctx, int session,
                          RtpMidiCallback callback, void *user_data)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return;
    ctx->callbacks[session] = callback;
    ctx->callback_data[session] = user_data;
}

bool rtpmidi_send_message(RtpMidiContext *ctx, int session,
                          uint8_t status, uint8_t data1, uint8_t data2)
{
    (void)status;
    (void)data1;
    (void)data2;
    return ctx && ctx->alive && session >= 0 &&
           session < RTPMIDI_MAX_SESSIONS && ctx->session_active[session];
}

bool rtpmidi_send_raw(RtpMidiContext *ctx, int session,
                      const uint8_t *data, size_t length)
{
    return ctx && ctx->alive && session >= 0 &&
           session < RTPMIDI_MAX_SESSIONS && ctx->session_active[session] &&
           data && length > 0;
}

void rtpmidi_poll(RtpMidiContext *ctx)
{
    (void)ctx;
}

int rtpmidi_get_session_count(RtpMidiContext *ctx)
{
    int count = 0;
    if (!ctx || !ctx->alive)
        return 0;
    for (int session = 0; session < RTPMIDI_MAX_SESSIONS; session++)
        count += ctx->session_active[session] ? 1 : 0;
    return count;
}

bool rtpmidi_get_session_info(RtpMidiContext *ctx, int index,
                              RtpMidiSessionInfo *info)
{
    int current = 0;
    if (!ctx || !ctx->alive || !info || index < 0)
        return false;
    for (int session = 0; session < RTPMIDI_MAX_SESSIONS; session++)
    {
        if (!ctx->session_active[session])
            continue;
        if (current++ != index)
            continue;
        memset(info, 0, sizeof(*info));
        info->handle = session;
        snprintf(info->name, sizeof(info->name), "%s",
                 ctx->session_names[session]);
        info->port = ctx->session_ports[session];
        info->state = RTPMIDI_STATE_LISTENING;
        return true;
    }
    return false;
}

const char *rtpmidi_get_error(RtpMidiContext *ctx)
{
    return ctx ? ctx->error : "Invalid RTP-MIDI context";
}

RtpMidiContext *rtpmidi_mock_context(int index)
{
    return index >= 0 && index < rtpmidi_context_count
               ? rtpmidi_contexts[index]
               : NULL;
}

int rtpmidi_mock_live_count(void)
{
    return rtpmidi_live_count_value;
}

int rtpmidi_mock_destroy_count(void)
{
    return rtpmidi_destroy_count_value;
}

bool rtpmidi_mock_emit(RtpMidiContext *ctx, int session,
                       const MidiMessage *message)
{
    if (!ctx || !ctx->alive || !message || session < 0 ||
        session >= RTPMIDI_MAX_SESSIONS || !ctx->session_active[session] ||
        !ctx->callbacks[session])
        return false;
    ctx->callbacks[session](session, message, ctx->callback_data[session]);
    return true;
}

bool rtpmidi_mock_callback_attached(RtpMidiContext *ctx, int session)
{
    return ctx && session >= 0 && session < RTPMIDI_MAX_SESSIONS &&
           ctx->callbacks[session] != NULL;
}

const char *rtpmidi_mock_session_name(RtpMidiContext *ctx, int session)
{
    return ctx && session >= 0 && session < RTPMIDI_MAX_SESSIONS &&
                   ctx->session_active[session]
               ? ctx->session_names[session]
               : NULL;
}