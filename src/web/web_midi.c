#include "midi/midi_wrapper.h"
#include "midi/midi_topology.h"
#include "midi/rtpmidi.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    bool active;
    int device_index;
    MidiCallback callback;
    void *user_data;
    MidiSysExCallback sysex_callback;
    void *sysex_user_data;
} WebMidiInputDevice;

typedef struct
{
    bool active;
    int device_index;
} WebMidiOutputDevice;

struct MidiContext
{
    WebMidiInputDevice inputs[MIDI_MAX_DEVICES];
    WebMidiOutputDevice outputs[MIDI_MAX_DEVICES];
    int input_handle_count;
    int output_handle_count;
    char error_msg[256];
};

static MidiContext *g_midi_web_ctx = NULL;
static int g_midi_web_ctx_refs = 0;

EM_JS(int, js_hw_midi_init, (void), {
    if (Module._hw_midi)
        return 1;

    Module._hw_midi = {
        access : null,
        ready : false,
        denied : false,
        inputs : [],
        outputs : [],
        inputHandles : {},
        outputHandles : {},
        generation : 0
    };

    if (!navigator.requestMIDIAccess)
    {
        console.warn("[budo-web] Web MIDI API not available in this browser.");
        Module._hw_midi.denied = true;
        return 0;
    }

    navigator.requestMIDIAccess({sysex : true}).then(function(access) {
        Module._hw_midi.access = access;
        Module._hw_midi.ready = true;

        Module._hw_midi_refresh_devices();

        access.onstatechange = function(e) {
            Module._hw_midi_refresh_devices();
            Module._hw_midi.generation++;
        };

        console.log("[budo-web] Web MIDI access granted (" +
            Module._hw_midi.inputs.length + " inputs, " +
            Module._hw_midi.outputs.length + " outputs)"); }, function(err) {
        
        navigator.requestMIDIAccess({ sysex : false }).then(function(access) {
            Module._hw_midi.access = access;
            Module._hw_midi.ready = true;
            Module._hw_midi_refresh_devices();
            access.onstatechange = function(e) {
                Module._hw_midi_refresh_devices();
                Module._hw_midi.generation++;
            };
            console.log("[budo-web] Web MIDI access granted without SysEx (" +
                Module._hw_midi.inputs.length + " inputs, " +
                Module._hw_midi.outputs.length + " outputs)");
        }, function(err2) {
            Module._hw_midi.denied = true;
            console.warn("[budo-web] Web MIDI access denied: " + err2.message);
        }); });

    Module._hw_midi_refresh_devices = function()
    {
        var m = Module._hw_midi;
        if (!m.access)
            return;
        m.inputs = [];
        m.outputs = [];
        m.access.inputs.forEach(function(input) {
            m.inputs.push(input);
        });
        m.access.outputs.forEach(function(output) {
            m.outputs.push(output);
        });
    };

    return 1;
});

EM_JS(int, js_hw_midi_is_ready, (void), {
    var m = Module._hw_midi;
    if (!m)
        return 0;
    return m.ready ? 1 : 0;
});

EM_JS(double, js_hw_midi_get_device_generation, (void), {
    var m = Module._hw_midi;
    return m ? m.generation : 0;
});

EM_JS(void, js_hw_midi_destroy, (void), {
    var m = Module._hw_midi;
    if (!m)
        return;

    var handles = Object.keys(m.inputHandles);
    for (var i = 0; i < handles.length; i++)
    {
        var h = handles[i];
        var input = m.inputHandles[h];
        if (input)
            input.onmidimessage = null;
    }
    m.inputHandles = {};
    m.outputHandles = {};

    if (m.access && m.access.onstatechange)
        m.access.onstatechange = null;

    Module._hw_midi = null;
});

EM_JS(int, js_hw_midi_get_input_count, (void), {
    var m = Module._hw_midi;
    if (!m || !m.ready)
        return 0;
    return m.inputs.length;
});

EM_JS(int, js_hw_midi_get_output_count, (void), {
    var m = Module._hw_midi;
    if (!m || !m.ready)
        return 0;
    return m.outputs.length;
});

EM_JS(int, js_hw_midi_get_input_name, (int index, char *buf, int buflen), {
    var m = Module._hw_midi;
    if (!m || !m.ready || index < 0 || index >= m.inputs.length)
        return 0;
    var name = m.inputs[index].name || "Unknown MIDI Input";
    var bytes = lengthBytesUTF8(name);
    if (bytes >= buflen)
        bytes = buflen - 1;
    stringToUTF8(name, buf, buflen);
    return 1;
});

EM_JS(int, js_hw_midi_get_output_name, (int index, char *buf, int buflen), {
    var m = Module._hw_midi;
    if (!m || !m.ready || index < 0 || index >= m.outputs.length)
        return 0;
    var name = m.outputs[index].name || "Unknown MIDI Output";
    var bytes = lengthBytesUTF8(name);
    if (bytes >= buflen)
        bytes = buflen - 1;
    stringToUTF8(name, buf, buflen);
    return 1;
});

EM_JS(int, js_hw_midi_open_input, (int device_index, int handle), {
    var m = Module._hw_midi;
    if (!m || !m.ready || device_index < 0 || device_index >= m.inputs.length)
        return 0;

    var input = m.inputs[device_index];

    input.onmidimessage = function(event)
    {
        var data = event.data;
        if (!data || data.length < 1)
            return;

        var status = data[0];

        if (status == 0xF0)
        {
            
            var len = data.length;
            if (len > 4096)
                len = 4096;
            var ptr = Module._malloc(len);
            if (ptr)
            {
                Module.HEAPU8.set(data.subarray(0, len), ptr);
                Module._web_midi_on_sysex(handle, ptr, len);
                Module._free(ptr);
            }
            return;
        }

        var d1 = data.length > 1 ? data[1] : 0;
        var d2 = data.length > 2 ? data[2] : 0;
        var timestampUs = 0;
        if (typeof event.timeStamp == 'number')
            timestampUs = event.timeStamp * 1000;
        Module._web_midi_on_message(handle, status, d1, d2, timestampUs);
    };

    m.inputHandles[handle] = input;
    return 1;
});

EM_JS(void, js_hw_midi_close_input, (int handle), {
    var m = Module._hw_midi;
    if (!m)
        return;
    var input = m.inputHandles[handle];
    if (input)
    {
        input.onmidimessage = null;
        delete m.inputHandles[handle];
    }
});

EM_JS(int, js_hw_midi_open_output, (int device_index, int handle), {
    var m = Module._hw_midi;
    if (!m || !m.ready || device_index < 0 || device_index >= m.outputs.length)
        return 0;
    m.outputHandles[handle] = m.outputs[device_index];
    return 1;
});

EM_JS(void, js_hw_midi_close_output, (int handle), {
    var m = Module._hw_midi;
    if (!m)
        return;
    delete m.outputHandles[handle];
});

EM_JS(int, js_hw_midi_send_message, (int handle, int status, int data1, int data2), {
    var m = Module._hw_midi;
    if (!m)
        return 0;
    var output = m.outputHandles[handle];
    if (!output)
        return 0;
    try
    {
        
        var s = status & 0xF0;
        if (s == 0xC0 || s == 0xD0)
        {
            
            output.send([ status, data1 ]);
        }
        else
        {
            output.send([ status, data1, data2 ]);
        }
        return 1;
    }
    catch(e)
    {
        console.warn("[budo-web] MIDI send error: " + e.message);
        return 0;
    }
});

EM_JS(int, js_hw_midi_send_raw, (int handle, const uint8_t *data, int length), {
    var m = Module._hw_midi;
    if (!m)
        return 0;
    var output = m.outputHandles[handle];
    if (!output)
        return 0;
    try
    {
        var arr = new Uint8Array(Module.HEAPU8.buffer, data, length);
        
        output.send(new Uint8Array(arr));
        return 1;
    }
    catch(e)
    {
        console.warn("[budo-web] MIDI send raw error: " + e.message);
        return 0;
    }
});

EM_JS(void, js_hw_midi_refresh, (void), {
    if (Module._hw_midi && Module._hw_midi_refresh_devices)
        Module._hw_midi_refresh_devices();
});

EMSCRIPTEN_KEEPALIVE
void web_midi_on_message(int handle, int status, int data1, int data2, double timestamp_us)
{
    if (!g_midi_web_ctx)
        return;
    if (handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    WebMidiInputDevice *dev = &g_midi_web_ctx->inputs[handle];
    if (!dev->active || !dev->callback)
        return;

    MidiMessage msg;
    msg.status = (uint8_t)status;
    msg.data1 = (uint8_t)data1;
    msg.data2 = (uint8_t)data2;
    msg.timestamp = timestamp_us > 0 ? (uint64_t)timestamp_us : 0;

    dev->callback(handle, &msg, dev->user_data);
}

EMSCRIPTEN_KEEPALIVE
void web_midi_on_sysex(int handle, const uint8_t *data, int length)
{
    if (!g_midi_web_ctx)
        return;
    if (handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    WebMidiInputDevice *dev = &g_midi_web_ctx->inputs[handle];
    if (!dev->active || !dev->sysex_callback)
        return;

    dev->sysex_callback(handle, data, (size_t)length, dev->sysex_user_data);
}

MidiContext *midi_create(void)
{
    if (g_midi_web_ctx)
    {
        g_midi_web_ctx_refs++;
        return g_midi_web_ctx;
    }

    MidiContext *ctx = (MidiContext *)calloc(1, sizeof(MidiContext));
    if (!ctx)
        return NULL;

    g_midi_web_ctx = ctx;
    g_midi_web_ctx_refs = 1;

    js_hw_midi_init();

    return ctx;
}

void midi_destroy(MidiContext *ctx)
{
    if (!ctx)
        return;
    if (ctx == g_midi_web_ctx && --g_midi_web_ctx_refs > 0)
        return;

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->inputs[i].active)
            midi_close_input(ctx, i);
        if (ctx->outputs[i].active)
            midi_close_output(ctx, i);
    }

    js_hw_midi_destroy();
    free(ctx);

    if (g_midi_web_ctx == ctx)
    {
        g_midi_web_ctx = NULL;
        g_midi_web_ctx_refs = 0;
    }
}

int midi_get_input_count(MidiContext *ctx)
{
    if (!ctx)
        return 0;
    return js_hw_midi_get_input_count();
}

int midi_get_output_count(MidiContext *ctx)
{
    if (!ctx)
        return 0;
    return js_hw_midi_get_output_count();
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0)
        return false;

    memset(info, 0, sizeof(MidiDeviceInfo));
    info->id = index;
    info->is_input = true;
    info->is_output = false;

    if (!js_hw_midi_get_input_name(index, info->name, MIDI_MAX_DEVICE_NAME))
        return false;

    return true;
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0)
        return false;

    memset(info, 0, sizeof(MidiDeviceInfo));
    info->id = index;
    info->is_input = false;
    info->is_output = true;

    if (!js_hw_midi_get_output_name(index, info->name, MIDI_MAX_DEVICE_NAME))
        return false;

    return true;
}

void midi_refresh_devices(MidiContext *ctx)
{
    if (!ctx)
        return;
    js_hw_midi_refresh();
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    (void)ctx;
    return (uint64_t)js_hw_midi_get_device_generation();
}

uint64_t midi_get_device_topology_fingerprint(MidiContext *ctx)
{
    if (!ctx)
        return 0;
    int input_count = midi_get_input_count(ctx);
    int output_count = midi_get_output_count(ctx);
    if (input_count > MIDI_MAX_DEVICES)
        input_count = MIDI_MAX_DEVICES;
    if (output_count > MIDI_MAX_DEVICES)
        output_count = MIDI_MAX_DEVICES;
    MidiDeviceInfo inputs[MIDI_MAX_DEVICES];
    MidiDeviceInfo outputs[MIDI_MAX_DEVICES];
    size_t valid_inputs = 0;
    size_t valid_outputs = 0;
    for (int i = 0; i < input_count; i++)
    {
        if (midi_get_input_info(ctx, i, &inputs[valid_inputs]))
            valid_inputs++;
    }
    for (int i = 0; i < output_count; i++)
    {
        if (midi_get_output_info(ctx, i, &outputs[valid_outputs]))
            valid_outputs++;
    }
    return midi_topology_fingerprint(inputs, valid_inputs, outputs, valid_outputs);
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data)
{
    if (!ctx || !callback)
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->inputs[i].active)
        {
            handle = i;
            break;
        }
    }

    if (handle < 0)
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No free input slots");
        return -1;
    }

    if (!js_hw_midi_open_input(index, handle))
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Failed to open MIDI input %d", index);
        return -1;
    }

    ctx->inputs[handle].active = true;
    ctx->inputs[handle].device_index = index;
    ctx->inputs[handle].callback = callback;
    ctx->inputs[handle].user_data = user_data;
    ctx->inputs[handle].sysex_callback = NULL;
    ctx->inputs[handle].sysex_user_data = NULL;
    ctx->input_handle_count++;

    return handle;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;
    if (!ctx->inputs[handle].active)
        return;

    js_hw_midi_close_input(handle);

    ctx->inputs[handle].active = false;
    ctx->inputs[handle].callback = NULL;
    ctx->inputs[handle].user_data = NULL;
    ctx->inputs[handle].sysex_callback = NULL;
    ctx->inputs[handle].sysex_user_data = NULL;
    ctx->input_handle_count--;
}

bool midi_set_sysex_callback(MidiContext *ctx, int handle,
                             MidiSysExCallback callback, void *user_data)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;
    if (!ctx->inputs[handle].active)
        return false;

    ctx->inputs[handle].sysex_callback = callback;
    ctx->inputs[handle].sysex_user_data = user_data;
    return true;
}

int midi_open_output(MidiContext *ctx, int index)
{
    if (!ctx)
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->outputs[i].active)
        {
            handle = i;
            break;
        }
    }

    if (handle < 0)
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No free output slots");
        return -1;
    }

    if (!js_hw_midi_open_output(index, handle))
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Failed to open MIDI output %d", index);
        return -1;
    }

    ctx->outputs[handle].active = true;
    ctx->outputs[handle].device_index = index;
    ctx->output_handle_count++;

    return handle;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;
    if (!ctx->outputs[handle].active)
        return;

    js_hw_midi_close_output(handle);

    ctx->outputs[handle].active = false;
    ctx->output_handle_count--;
}

bool midi_send_message(MidiContext *ctx, int handle,
                       uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;
    if (!ctx->outputs[handle].active)
        return false;

    return js_hw_midi_send_message(handle, status, data1, data2) != 0;
}

bool midi_send_raw(MidiContext *ctx, int handle,
                   const uint8_t *data, size_t length)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;
    if (!ctx->outputs[handle].active)
        return false;
    if (!data || length == 0)
        return false;

    return js_hw_midi_send_raw(handle, data, (int)length) != 0;
}

bool midi_note_on(MidiContext *ctx, int handle, int channel, int note, int velocity)
{
    return midi_send_message(ctx, handle, MIDI_NOTE_ON | (channel & 0x0F),
                             note & 0x7F, velocity & 0x7F);
}

bool midi_note_off(MidiContext *ctx, int handle, int channel, int note, int velocity)
{
    return midi_send_message(ctx, handle, MIDI_NOTE_OFF | (channel & 0x0F),
                             note & 0x7F, velocity & 0x7F);
}

bool midi_control_change(MidiContext *ctx, int handle, int channel, int control, int value)
{
    return midi_send_message(ctx, handle, MIDI_CONTROL_CHANGE | (channel & 0x0F),
                             control & 0x7F, value & 0x7F);
}

bool midi_program_change(MidiContext *ctx, int handle, int channel, int program)
{
    return midi_send_message(ctx, handle, MIDI_PROGRAM_CHANGE | (channel & 0x0F),
                             program & 0x7F, 0);
}

bool midi_pitch_bend(MidiContext *ctx, int handle, int channel, int value)
{
    int bend = value + 8192;
    if (bend < 0)
        bend = 0;
    if (bend > 16383)
        bend = 16383;
    return midi_send_message(ctx, handle, MIDI_PITCH_BEND | (channel & 0x0F),
                             bend & 0x7F, (bend >> 7) & 0x7F);
}

bool midi_channel_pressure(MidiContext *ctx, int handle, int channel, int pressure)
{
    return midi_send_message(ctx, handle, MIDI_CHANNEL_PRESSURE | (channel & 0x0F),
                             pressure & 0x7F, 0);
}

bool midi_poly_pressure(MidiContext *ctx, int handle, int channel, int note, int pressure)
{
    return midi_send_message(ctx, handle, MIDI_POLY_PRESSURE | (channel & 0x0F),
                             note & 0x7F, pressure & 0x7F);
}

MidiMessageType midi_get_type(uint8_t status)
{
    if (status >= 0xF0)
        return MIDI_SYSTEM;
    return (MidiMessageType)(status & 0xF0);
}

int midi_get_channel(uint8_t status)
{
    if (status >= 0xF0)
        return -1;
    return status & 0x0F;
}

const char *midi_get_error(MidiContext *ctx)
{
    return ctx ? ctx->error_msg : "Invalid context";
}

RtpMidiContext *rtpmidi_create(UdpContext *udp_ctx)
{
    (void)udp_ctx;
    return NULL;
}

void rtpmidi_destroy(RtpMidiContext *ctx)
{
    (void)ctx;
}

int rtpmidi_create_session(RtpMidiContext *ctx, const char *name, int control_port)
{
    (void)ctx;
    (void)name;
    (void)control_port;
    return -1;
}

bool rtpmidi_connect(RtpMidiContext *ctx, int session, const char *host, int port)
{
    (void)ctx;
    (void)session;
    (void)host;
    (void)port;
    return false;
}

void rtpmidi_destroy_session(RtpMidiContext *ctx, int session)
{
    (void)ctx;
    (void)session;
}

void rtpmidi_set_callback(RtpMidiContext *ctx, int session,
                          RtpMidiCallback callback, void *user_data)
{
    (void)ctx;
    (void)session;
    (void)callback;
    (void)user_data;
}

bool rtpmidi_send_message(RtpMidiContext *ctx, int session,
                          uint8_t status, uint8_t data1, uint8_t data2)
{
    (void)ctx;
    (void)session;
    (void)status;
    (void)data1;
    (void)data2;
    return false;
}

bool rtpmidi_send_raw(RtpMidiContext *ctx, int session,
                      const uint8_t *data, size_t length)
{
    (void)ctx;
    (void)session;
    (void)data;
    (void)length;
    return false;
}

void rtpmidi_poll(RtpMidiContext *ctx)
{
    (void)ctx;
}

int rtpmidi_get_session_count(RtpMidiContext *ctx)
{
    (void)ctx;
    return 0;
}

bool rtpmidi_get_session_info(RtpMidiContext *ctx, int index, RtpMidiSessionInfo *info)
{
    (void)ctx;
    (void)index;
    (void)info;
    return false;
}

const char *rtpmidi_get_error(RtpMidiContext *ctx)
{
    (void)ctx;
    return "RTP-MIDI is not available on web (no raw UDP sockets)";
}