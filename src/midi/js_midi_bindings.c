#include "js_midi_bindings.h"
#include "midi_service.h"
#include "midi_topology.h"
#include "core/subsystem_queue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MIDI_MSG_QUEUE_SIZE 256
#define MIDI_SYSEX_QUEUE_SIZE 16
#define RTPMIDI_MSG_QUEUE_SIZE 256
#define MIDI_DEVICE_CHECK_FRAMES 60
#define MAX_MIDI_CALLBACKS MIDI_MAX_DEVICES
#define MAX_RTPMIDI_CALLBACKS RTPMIDI_MAX_SESSIONS

typedef struct
{
    int device_handle;
    MidiMessage message;
} MidiQueuedMessage;

typedef struct
{
    int device_handle;
    uint8_t data[MIDI_SYSEX_MAX_SIZE];
    size_t length;
} MidiQueuedSysEx;

typedef struct
{
    int session_handle;
    MidiMessage message;
} RtpMidiQueuedMessage;

struct JsMidiContext
{
    JSContext *js_ctx;
    MidiContext *midi_ctx;
    RtpMidiContext *rtpmidi_ctx;
    bool midi_lazy_initialized;
    SubsystemQueue midi_queue;
    SubsystemQueue sysex_queue;
    SubsystemQueue rtpmidi_queue;
    MidiQueuedMessage midi_storage[MIDI_MSG_QUEUE_SIZE];
    MidiQueuedSysEx sysex_storage[MIDI_SYSEX_QUEUE_SIZE];
    RtpMidiQueuedMessage rtpmidi_storage[RTPMIDI_MSG_QUEUE_SIZE];
    JSValue midi_callbacks[MAX_MIDI_CALLBACKS];
    bool midi_callbacks_active[MAX_MIDI_CALLBACKS];
    JSValue rtpmidi_callbacks[MAX_RTPMIDI_CALLBACKS];
    bool rtpmidi_callbacks_active[MAX_RTPMIDI_CALLBACKS];
    JSValue devices_changed_callback;
    MidiTopologyObserver topology_observer;
    int device_check_frames;
};

static JsMidiContext *js_midi_binding_state(
    JSContext *ctx, JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    JsMidiContext *state = NULL;

    if (data && size == sizeof(state))
        memcpy(&state, data, sizeof(state));
    return state;
}

static MidiContext *js_midi_context(JsMidiContext *state)
{
    if (!state)
        return NULL;
    if (!state->midi_lazy_initialized)
    {
        state->midi_lazy_initialized = true;
        state->midi_ctx = midi_create();
        if (!state->midi_ctx)
            fprintf(stderr, "Warning: Failed to create MIDI context\n");
    }
    return state->midi_ctx;
}

#define ensure_midi_ctx() ((void)js_midi_context(state))
#define g_midi_ctx (state->midi_ctx)
#define g_midi_js_jsctx (state->js_ctx)
#define g_rtpmidi_js_ctx (state->rtpmidi_ctx)
#define g_midi_callbacks (state->midi_callbacks)
#define g_midi_callbacks_active (state->midi_callbacks_active)
#define g_rtpmidi_callbacks (state->rtpmidi_callbacks)
#define g_rtpmidi_callbacks_active (state->rtpmidi_callbacks_active)
#define g_midi_devices_changed_callback (state->devices_changed_callback)
#define g_midi_topology_observer (state->topology_observer)
#define g_midi_device_check_frames (state->device_check_frames)

#define JS_MIDI_CALLBACK(name)                                           \
    static JSValue name(JSContext *ctx, JSValueConst this_val, int argc, \
                        JSValueConst *argv, int magic,                   \
                        JSValueConst *func_data)

#define JS_MIDI_STATE()                                           \
    JsMidiContext *state = js_midi_binding_state(ctx, func_data); \
    (void)magic;                                                  \
    (void)state

static JSValue js_midi_device_array(JSContext *ctx, MidiContext *midi_ctx, bool inputs)
{
    int count = inputs ? midi_get_input_count(midi_ctx) : midi_get_output_count(midi_ctx);
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < count; i++)
    {
        MidiDeviceInfo info;
        bool ok = inputs ? midi_get_input_info(midi_ctx, i, &info)
                         : midi_get_output_info(midi_ctx, i, &info);
        if (ok)
        {
            JSValue device = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, device, "id", JS_NewInt32(ctx, info.id));
            JS_SetPropertyStr(ctx, device, "name", JS_NewString(ctx, info.name));
            JS_SetPropertyUint32(ctx, arr, (uint32_t)i, device);
        }
    }
    return arr;
}

static void midi_input_callback(int device_id, const MidiMessage *message, void *user_data)
{
    JsMidiContext *state = (JsMidiContext *)user_data;
    MidiQueuedMessage queued;

    if (!state || !message)
        return;
    queued.device_handle = device_id;
    queued.message = *message;
    subsystem_queue_try_push(&state->midi_queue, &queued);
}

static void midi_sysex_callback(int device_id, const uint8_t *data, size_t length, void *user_data)
{
    JsMidiContext *state = (JsMidiContext *)user_data;
    MidiQueuedSysEx queued;

    if (!state || !data || length > MIDI_SYSEX_MAX_SIZE)
        return;
    queued.device_handle = device_id;
    memcpy(queued.data, data, length);
    queued.length = length;
    subsystem_queue_try_push(&state->sysex_queue, &queued);
}

static bool midi_dequeue(JsMidiContext *state, MidiQueuedMessage *message)
{
    return state && subsystem_queue_try_pop(&state->midi_queue, message);
}

static bool midi_sysex_dequeue(JsMidiContext *state,
                               MidiQueuedSysEx *message)
{
    return state && subsystem_queue_try_pop(&state->sysex_queue, message);
}

JS_MIDI_CALLBACK(js_midi_get_input_count)
{
    JS_MIDI_STATE();
    (void)this_val;
    (void)argc;
    (void)argv;
    ensure_midi_ctx();
    if (!g_midi_ctx)
        return JS_NewInt32(ctx, 0);
    return JS_NewInt32(ctx, midi_get_input_count(g_midi_ctx));
}

JS_MIDI_CALLBACK(js_midi_get_output_count)
{
    JS_MIDI_STATE();
    (void)this_val;
    (void)argc;
    (void)argv;
    ensure_midi_ctx();
    if (!g_midi_ctx)
        return JS_NewInt32(ctx, 0);
    return JS_NewInt32(ctx, midi_get_output_count(g_midi_ctx));
}

JS_MIDI_CALLBACK(js_midi_get_input_devices)
{
    JS_MIDI_STATE();
    (void)this_val;
    (void)argc;
    (void)argv;
    ensure_midi_ctx();
    if (!g_midi_ctx)
        return JS_NewArray(ctx);

    return js_midi_device_array(ctx, g_midi_ctx, true);
}

JS_MIDI_CALLBACK(js_midi_get_output_devices)
{
    JS_MIDI_STATE();
    (void)this_val;
    (void)argc;
    (void)argv;
    ensure_midi_ctx();
    if (!g_midi_ctx)
        return JS_NewArray(ctx);

    return js_midi_device_array(ctx, g_midi_ctx, false);
}

JS_MIDI_CALLBACK(js_midi_refresh_devices)
{
    JS_MIDI_STATE();
    (void)this_val;
    (void)argc;
    (void)argv;
    ensure_midi_ctx();
    if (g_midi_ctx)
    {
        midi_refresh_devices(g_midi_ctx);
    }
    return JS_UNDEFINED;
}

JS_MIDI_CALLBACK(js_midi_on_devices_changed)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (argc != 1 || (!JS_IsFunction(ctx, argv[0]) && !JS_IsNull(argv[0])))
        return JS_ThrowTypeError(ctx, "midi.onDevicesChanged expects a callback or null");

    if (!JS_IsUndefined(g_midi_devices_changed_callback))
        JS_FreeValue(ctx, g_midi_devices_changed_callback);
    g_midi_devices_changed_callback = JS_UNDEFINED;
    g_midi_device_check_frames = 0;

    if (JS_IsNull(argv[0]))
        return JS_UNDEFINED;

    ensure_midi_ctx();
    if (!g_midi_ctx)
        return JS_UNDEFINED;
    midi_refresh_devices(g_midi_ctx);
    midi_topology_observer_reset(&g_midi_topology_observer,
                                 midi_get_device_topology_fingerprint(g_midi_ctx),
                                 midi_get_device_generation(g_midi_ctx));
    g_midi_devices_changed_callback = JS_DupValue(ctx, argv[0]);
    return JS_UNDEFINED;
}

JS_MIDI_CALLBACK(js_midi_open_input)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 2)
        return JS_NewInt32(ctx, -1);

    int device_index;
    JS_ToInt32(ctx, &device_index, argv[0]);

    int handle = midi_open_input(g_midi_ctx, device_index, midi_input_callback, state);
    if (handle < 0)
        return JS_NewInt32(ctx, -1);

    midi_set_sysex_callback(g_midi_ctx, handle, midi_sysex_callback, state);

    if (handle >= 0 && handle < MAX_MIDI_CALLBACKS)
    {
        if (g_midi_callbacks_active[handle])
        {
            JS_FreeValue(ctx, g_midi_callbacks[handle]);
        }
        g_midi_callbacks[handle] = JS_DupValue(ctx, argv[1]);
        g_midi_callbacks_active[handle] = true;
    }

    return JS_NewInt32(ctx, handle);
}

JS_MIDI_CALLBACK(js_midi_close_input)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 1)
        return JS_UNDEFINED;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    if (handle >= 0 && handle < MAX_MIDI_CALLBACKS && g_midi_callbacks_active[handle])
    {
        JS_FreeValue(ctx, g_midi_callbacks[handle]);
        g_midi_callbacks_active[handle] = false;
    }

    midi_close_input(g_midi_ctx, handle);
    return JS_UNDEFINED;
}

JS_MIDI_CALLBACK(js_midi_open_output)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    int device_index;
    JS_ToInt32(ctx, &device_index, argv[0]);

    ApiError error;
    return JS_NewInt32(ctx, midi_service_open_output(g_midi_ctx, device_index,
                                                     &error));
}

JS_MIDI_CALLBACK(js_midi_close_output)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 1)
        return JS_UNDEFINED;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);
    midi_close_output(g_midi_ctx, handle);
    return JS_UNDEFINED;
}

JS_MIDI_CALLBACK(js_midi_send_message)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 4)
        return JS_FALSE;

    int handle, status, data1, data2;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &status, argv[1]);
    JS_ToInt32(ctx, &data1, argv[2]);
    JS_ToInt32(ctx, &data2, argv[3]);

    bool result = midi_send_message(g_midi_ctx, handle, (uint8_t)status,
                                    (uint8_t)data1, (uint8_t)data2);
    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_send_raw)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 2)
        return JS_FALSE;

    int handle;
    JS_ToInt32(ctx, &handle, argv[0]);

    JSValue arr = argv[1];
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int len;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    if (len <= 0 || len > MIDI_SYSEX_MAX_SIZE)
        return JS_FALSE;

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf)
        return JS_FALSE;

    for (int i = 0; i < len; i++)
    {
        JSValue elem = JS_GetPropertyUint32(ctx, arr, i);
        int val;
        JS_ToInt32(ctx, &val, elem);
        JS_FreeValue(ctx, elem);
        buf[i] = (uint8_t)val;
    }

    bool result = midi_send_raw(g_midi_ctx, handle, buf, len);
    free(buf);

    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_note_on)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 4)
        return JS_FALSE;

    int handle, channel, note, velocity;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &note, argv[2]);
    JS_ToInt32(ctx, &velocity, argv[3]);

    return JS_NewBool(ctx, midi_note_on(g_midi_ctx, handle, channel, note, velocity));
}

JS_MIDI_CALLBACK(js_midi_note_off)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 4)
        return JS_FALSE;

    int handle, channel, note, velocity;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &note, argv[2]);
    JS_ToInt32(ctx, &velocity, argv[3]);

    return JS_NewBool(ctx, midi_note_off(g_midi_ctx, handle, channel, note, velocity));
}

JS_MIDI_CALLBACK(js_midi_control_change)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 4)
        return JS_FALSE;

    int handle, channel, control, value;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &control, argv[2]);
    JS_ToInt32(ctx, &value, argv[3]);

    return JS_NewBool(ctx, midi_control_change(g_midi_ctx, handle, channel, control, value));
}

JS_MIDI_CALLBACK(js_midi_program_change)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 3)
        return JS_FALSE;

    int handle, channel, program;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &program, argv[2]);

    return JS_NewBool(ctx, midi_program_change(g_midi_ctx, handle, channel, program));
}

JS_MIDI_CALLBACK(js_midi_pitch_bend)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 3)
        return JS_FALSE;

    int handle, channel, value;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &value, argv[2]);

    return JS_NewBool(ctx, midi_pitch_bend(g_midi_ctx, handle, channel, value));
}

JS_MIDI_CALLBACK(js_midi_channel_pressure)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 3)
        return JS_FALSE;

    int handle, channel, pressure;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &pressure, argv[2]);

    return JS_NewBool(ctx, midi_channel_pressure(g_midi_ctx, handle, channel, pressure));
}

JS_MIDI_CALLBACK(js_midi_poly_pressure)
{
    JS_MIDI_STATE();
    (void)this_val;
    ensure_midi_ctx();
    if (!g_midi_ctx || argc < 4)
        return JS_FALSE;

    int handle, channel, note, pressure;
    JS_ToInt32(ctx, &handle, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &note, argv[2]);
    JS_ToInt32(ctx, &pressure, argv[3]);

    return JS_NewBool(ctx, midi_poly_pressure(g_midi_ctx, handle, channel, note, pressure));
}

static void rtpmidi_input_callback(int session_handle, const MidiMessage *message, void *user_data)
{
    JsMidiContext *state = (JsMidiContext *)user_data;
    RtpMidiQueuedMessage queued;

    if (!state || !message)
        return;
    queued.session_handle = session_handle;
    queued.message = *message;
    subsystem_queue_try_push(&state->rtpmidi_queue, &queued);
}

JS_MIDI_CALLBACK(js_midi_get_type)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (argc < 1)
        return JS_NewInt32(ctx, 0);

    int status;
    JS_ToInt32(ctx, &status, argv[0]);
    return JS_NewInt32(ctx, midi_get_type((uint8_t)status));
}

JS_MIDI_CALLBACK(js_midi_get_channel)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (argc < 1)
        return JS_NewInt32(ctx, -1);

    int status;
    JS_ToInt32(ctx, &status, argv[0]);
    return JS_NewInt32(ctx, midi_get_channel((uint8_t)status));
}

JS_MIDI_CALLBACK(js_midi_create_session)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_NewInt32(ctx, -1);

    int port = 0;
    if (argc >= 2)
        JS_ToInt32(ctx, &port, argv[1]);

    int handle = rtpmidi_create_session(g_rtpmidi_js_ctx, name, port);
    JS_FreeCString(ctx, name);

    return JS_NewInt32(ctx, handle);
}

JS_MIDI_CALLBACK(js_midi_connect_session)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 3)
        return JS_FALSE;

    int session;
    JS_ToInt32(ctx, &session, argv[0]);

    const char *host = JS_ToCString(ctx, argv[1]);
    if (!host)
        return JS_FALSE;

    int port;
    JS_ToInt32(ctx, &port, argv[2]);

    bool result = rtpmidi_connect(g_rtpmidi_js_ctx, session, host, port);
    JS_FreeCString(ctx, host);

    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_destroy_session)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 1)
        return JS_UNDEFINED;

    int session;
    JS_ToInt32(ctx, &session, argv[0]);

    if (session >= 0 && session < MAX_RTPMIDI_CALLBACKS && g_rtpmidi_callbacks_active[session])
    {
        JS_FreeValue(ctx, g_rtpmidi_callbacks[session]);
        g_rtpmidi_callbacks_active[session] = false;
    }

    rtpmidi_destroy_session(g_rtpmidi_js_ctx, session);
    return JS_UNDEFINED;
}

JS_MIDI_CALLBACK(js_midi_on_session_message)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 2)
        return JS_UNDEFINED;

    int session;
    JS_ToInt32(ctx, &session, argv[0]);

    if (session < 0 || session >= MAX_RTPMIDI_CALLBACKS)
        return JS_UNDEFINED;

    if (g_rtpmidi_callbacks_active[session])
    {
        JS_FreeValue(ctx, g_rtpmidi_callbacks[session]);
        g_rtpmidi_callbacks_active[session] = false;
    }

    if (JS_IsFunction(ctx, argv[1]))
    {
        g_rtpmidi_callbacks[session] = JS_DupValue(ctx, argv[1]);
        g_rtpmidi_callbacks_active[session] = true;

        rtpmidi_set_callback(g_rtpmidi_js_ctx, session, rtpmidi_input_callback, state);
    }

    return JS_UNDEFINED;
}

JS_MIDI_CALLBACK(js_midi_session_send)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 4)
        return JS_FALSE;

    int session, status, data1, data2;
    JS_ToInt32(ctx, &session, argv[0]);
    JS_ToInt32(ctx, &status, argv[1]);
    JS_ToInt32(ctx, &data1, argv[2]);
    JS_ToInt32(ctx, &data2, argv[3]);

    bool result = rtpmidi_send_message(g_rtpmidi_js_ctx, session,
                                       (uint8_t)status, (uint8_t)data1, (uint8_t)data2);
    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_session_note_on)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 4)
        return JS_FALSE;

    int session, channel, note, velocity;
    JS_ToInt32(ctx, &session, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &note, argv[2]);
    JS_ToInt32(ctx, &velocity, argv[3]);

    uint8_t status = MIDI_NOTE_ON | (channel & 0x0F);
    bool result = rtpmidi_send_message(g_rtpmidi_js_ctx, session,
                                       status, (uint8_t)(note & 0x7F),
                                       (uint8_t)(velocity & 0x7F));
    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_session_note_off)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 4)
        return JS_FALSE;

    int session, channel, note, velocity;
    JS_ToInt32(ctx, &session, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &note, argv[2]);
    JS_ToInt32(ctx, &velocity, argv[3]);

    uint8_t status = MIDI_NOTE_OFF | (channel & 0x0F);
    bool result = rtpmidi_send_message(g_rtpmidi_js_ctx, session,
                                       status, (uint8_t)(note & 0x7F),
                                       (uint8_t)(velocity & 0x7F));
    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_session_cc)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 4)
        return JS_FALSE;

    int session, channel, control, value;
    JS_ToInt32(ctx, &session, argv[0]);
    JS_ToInt32(ctx, &channel, argv[1]);
    JS_ToInt32(ctx, &control, argv[2]);
    JS_ToInt32(ctx, &value, argv[3]);

    uint8_t status = MIDI_CONTROL_CHANGE | (channel & 0x0F);
    bool result = rtpmidi_send_message(g_rtpmidi_js_ctx, session,
                                       status, (uint8_t)(control & 0x7F),
                                       (uint8_t)(value & 0x7F));
    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_session_send_raw)
{
    JS_MIDI_STATE();
    (void)this_val;
    if (!g_rtpmidi_js_ctx || argc < 2)
        return JS_FALSE;

    int session;
    JS_ToInt32(ctx, &session, argv[0]);

    JSValue arr = argv[1];
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int len;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    if (len <= 0 || len > 512)
        return JS_FALSE;

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf)
        return JS_FALSE;

    for (int i = 0; i < len; i++)
    {
        JSValue elem = JS_GetPropertyUint32(ctx, arr, i);
        int val;
        JS_ToInt32(ctx, &val, elem);
        JS_FreeValue(ctx, elem);
        buf[i] = (uint8_t)val;
    }

    bool result = rtpmidi_send_raw(g_rtpmidi_js_ctx, session, buf, len);
    free(buf);

    return JS_NewBool(ctx, result);
}

JS_MIDI_CALLBACK(js_midi_get_sessions)
{
    JS_MIDI_STATE();
    (void)this_val;
    (void)argc;
    (void)argv;

    JSValue arr = JS_NewArray(ctx);
    if (!g_rtpmidi_js_ctx)
        return arr;

    int count = rtpmidi_get_session_count(g_rtpmidi_js_ctx);
    for (int i = 0; i < count; i++)
    {
        RtpMidiSessionInfo info;
        if (rtpmidi_get_session_info(g_rtpmidi_js_ctx, i, &info))
        {
            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "handle", JS_NewInt32(ctx, info.handle));
            JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, info.name));
            JS_SetPropertyStr(ctx, obj, "port", JS_NewInt32(ctx, info.port));
            const char *state_str = "unknown";
            switch (info.state)
            {
            case RTPMIDI_STATE_IDLE:
                state_str = "idle";
                break;
            case RTPMIDI_STATE_LISTENING:
                state_str = "listening";
                break;
            case RTPMIDI_STATE_INVITING_CTRL:
            case RTPMIDI_STATE_INVITING_DATA:
                state_str = "connecting";
                break;
            case RTPMIDI_STATE_CONNECTED:
                state_str = "connected";
                break;
            case RTPMIDI_STATE_CLOSED:
                state_str = "closed";
                break;
            }
            JS_SetPropertyStr(ctx, obj, "state", JS_NewString(ctx, state_str));
            JS_SetPropertyStr(ctx, obj, "peerCount", JS_NewInt32(ctx, info.peer_count));
            JS_SetPropertyUint32(ctx, arr, i, obj);
        }
    }

    return arr;
}

typedef struct
{
    const char *name;
    int length;
    JSCFunctionData *callback;
} JsMidiFunction;

static const JsMidiFunction js_midi_funcs[] = {
    
    {"getInputCount", 0, js_midi_get_input_count},
    {"getOutputCount", 0, js_midi_get_output_count},
    {"getInputDevices", 0, js_midi_get_input_devices},
    {"getOutputDevices", 0, js_midi_get_output_devices},
    {"refreshDevices", 0, js_midi_refresh_devices},
    {"onDevicesChanged", 1, js_midi_on_devices_changed},

    {"openInput", 2, js_midi_open_input},
    {"closeInput", 1, js_midi_close_input},

    {"openOutput", 1, js_midi_open_output},
    {"closeOutput", 1, js_midi_close_output},
    {"sendMessage", 4, js_midi_send_message},
    {"sendRaw", 2, js_midi_send_raw},

    {"noteOn", 4, js_midi_note_on},
    {"noteOff", 4, js_midi_note_off},
    {"controlChange", 4, js_midi_control_change},
    {"programChange", 3, js_midi_program_change},
    {"pitchBend", 3, js_midi_pitch_bend},
    {"channelPressure", 3, js_midi_channel_pressure},
    {"polyPressure", 4, js_midi_poly_pressure},

    {"getType", 1, js_midi_get_type},
    {"getChannel", 1, js_midi_get_channel},

    {"createSession", 2, js_midi_create_session},
    {"connectSession", 3, js_midi_connect_session},
    {"destroySession", 1, js_midi_destroy_session},
    {"onSessionMessage", 2, js_midi_on_session_message},
    {"sessionSend", 4, js_midi_session_send},
    {"sessionNoteOn", 4, js_midi_session_note_on},
    {"sessionNoteOff", 4, js_midi_session_note_off},
    {"sessionControlChange", 4, js_midi_session_cc},
    {"sessionSendRaw", 2, js_midi_session_send_raw},
    {"getSessions", 0, js_midi_get_sessions},
};

static int js_midi_add_function(JSContext *ctx, JSValue midi_obj,
                                const JsMidiFunction *definition,
                                JsMidiContext *state)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&state,
                                         sizeof(state));
    if (JS_IsException(data))
        return -1;
    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;
    return JS_SetPropertyStr(ctx, midi_obj, definition->name, function);
}

static void js_midi_add_constants(JSContext *ctx, JSValue midi_obj)
{
    
    JS_SetPropertyStr(ctx, midi_obj, "NOTE_OFF", JS_NewInt32(ctx, MIDI_NOTE_OFF));
    JS_SetPropertyStr(ctx, midi_obj, "NOTE_ON", JS_NewInt32(ctx, MIDI_NOTE_ON));
    JS_SetPropertyStr(ctx, midi_obj, "POLY_PRESSURE", JS_NewInt32(ctx, MIDI_POLY_PRESSURE));
    JS_SetPropertyStr(ctx, midi_obj, "CONTROL_CHANGE", JS_NewInt32(ctx, MIDI_CONTROL_CHANGE));
    JS_SetPropertyStr(ctx, midi_obj, "PROGRAM_CHANGE", JS_NewInt32(ctx, MIDI_PROGRAM_CHANGE));
    JS_SetPropertyStr(ctx, midi_obj, "CHANNEL_PRESSURE", JS_NewInt32(ctx, MIDI_CHANNEL_PRESSURE));
    JS_SetPropertyStr(ctx, midi_obj, "PITCH_BEND", JS_NewInt32(ctx, MIDI_PITCH_BEND));
    JS_SetPropertyStr(ctx, midi_obj, "SYSEX", JS_NewInt32(ctx, 0xF0));

    JS_SetPropertyStr(ctx, midi_obj, "CC_MOD_WHEEL", JS_NewInt32(ctx, MIDI_CC_MOD_WHEEL));
    JS_SetPropertyStr(ctx, midi_obj, "CC_BREATH", JS_NewInt32(ctx, MIDI_CC_BREATH));
    JS_SetPropertyStr(ctx, midi_obj, "CC_FOOT", JS_NewInt32(ctx, MIDI_CC_FOOT));
    JS_SetPropertyStr(ctx, midi_obj, "CC_VOLUME", JS_NewInt32(ctx, MIDI_CC_VOLUME));
    JS_SetPropertyStr(ctx, midi_obj, "CC_PAN", JS_NewInt32(ctx, MIDI_CC_PAN));
    JS_SetPropertyStr(ctx, midi_obj, "CC_EXPRESSION", JS_NewInt32(ctx, MIDI_CC_EXPRESSION));
    JS_SetPropertyStr(ctx, midi_obj, "CC_SUSTAIN", JS_NewInt32(ctx, MIDI_CC_SUSTAIN));
    JS_SetPropertyStr(ctx, midi_obj, "CC_ALL_SOUND_OFF", JS_NewInt32(ctx, MIDI_CC_ALL_SOUND_OFF));
    JS_SetPropertyStr(ctx, midi_obj, "CC_ALL_NOTES_OFF", JS_NewInt32(ctx, MIDI_CC_ALL_NOTES_OFF));
}

JsMidiContext *js_midi_init(JSContext *ctx)
{
    JsMidiContext *state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->js_ctx = ctx;
    state->devices_changed_callback = JS_UNDEFINED;
    if (!subsystem_queue_init(&state->midi_queue, state->midi_storage,
                              sizeof(state->midi_storage[0]), MIDI_MSG_QUEUE_SIZE) ||
        !subsystem_queue_init(&state->sysex_queue, state->sysex_storage,
                              sizeof(state->sysex_storage[0]), MIDI_SYSEX_QUEUE_SIZE) ||
        !subsystem_queue_init(&state->rtpmidi_queue, state->rtpmidi_storage,
                              sizeof(state->rtpmidi_storage[0]), RTPMIDI_MSG_QUEUE_SIZE))
    {
        subsystem_queue_destroy(&state->rtpmidi_queue);
        subsystem_queue_destroy(&state->sysex_queue);
        subsystem_queue_destroy(&state->midi_queue);
        free(state);
        return NULL;
    }

    for (int i = 0; i < MAX_MIDI_CALLBACKS; i++)
    {
        g_midi_callbacks[i] = JS_UNDEFINED;
        g_midi_callbacks_active[i] = false;
    }
    for (int i = 0; i < MAX_RTPMIDI_CALLBACKS; i++)
    {
        g_rtpmidi_callbacks[i] = JS_UNDEFINED;
        g_rtpmidi_callbacks_active[i] = false;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");

    JSValue midi_obj = JS_NewObject(ctx);
    for (size_t index = 0; index < sizeof(js_midi_funcs) / sizeof(js_midi_funcs[0]); index++)
    {
        if (js_midi_add_function(ctx, midi_obj, &js_midi_funcs[index], state) < 0)
        {
            JS_FreeValue(ctx, midi_obj);
            JS_FreeValue(ctx, sys_obj);
            JS_FreeValue(ctx, global);
            js_midi_cleanup(state);
            return NULL;
        }
    }

    js_midi_add_constants(ctx, midi_obj);

    JS_SetPropertyStr(ctx, sys_obj, "midi", midi_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    return state;
}

void js_midi_cleanup(JsMidiContext *state)
{
    if (!state)
        return;

    subsystem_queue_close(&state->midi_queue);
    subsystem_queue_close(&state->sysex_queue);
    subsystem_queue_close(&state->rtpmidi_queue);

    if (state->rtpmidi_ctx)
    {
        for (int i = 0; i < MAX_RTPMIDI_CALLBACKS; i++)
            rtpmidi_set_callback(state->rtpmidi_ctx, i, NULL, NULL);
    }

    if (state->midi_ctx)
    {
        midi_destroy(state->midi_ctx);
        state->midi_ctx = NULL;
    }

    if (g_midi_js_jsctx)
    {
        
        for (int i = 0; i < MAX_MIDI_CALLBACKS; i++)
        {
            if (g_midi_callbacks_active[i])
            {
                JS_FreeValue(g_midi_js_jsctx, g_midi_callbacks[i]);
                g_midi_callbacks_active[i] = false;
            }
        }
        for (int i = 0; i < MAX_RTPMIDI_CALLBACKS; i++)
        {
            if (g_rtpmidi_callbacks_active[i])
            {
                JS_FreeValue(g_midi_js_jsctx, g_rtpmidi_callbacks[i]);
                g_rtpmidi_callbacks_active[i] = false;
            }
        }
        if (!JS_IsUndefined(g_midi_devices_changed_callback))
        {
            JS_FreeValue(g_midi_js_jsctx, g_midi_devices_changed_callback);
            g_midi_devices_changed_callback = JS_UNDEFINED;
        }
    }

    state->js_ctx = NULL;
    state->rtpmidi_ctx = NULL;
    subsystem_queue_destroy(&state->rtpmidi_queue);
    subsystem_queue_destroy(&state->sysex_queue);
    subsystem_queue_destroy(&state->midi_queue);
    free(state);
}

void js_midi_set_context(JsMidiContext *state,
                         MidiContext *midi_ctx, JSContext *js_ctx)
{
    if (!state)
        return;
    g_midi_ctx = midi_ctx;
    g_midi_js_jsctx = js_ctx;
    state->midi_lazy_initialized = midi_ctx != NULL;
}

void js_midi_set_rtpmidi(JsMidiContext *state, RtpMidiContext *rtp_ctx)
{
    if (!state)
        return;
    if (g_rtpmidi_js_ctx && g_rtpmidi_js_ctx != rtp_ctx)
    {
        for (int i = 0; i < MAX_RTPMIDI_CALLBACKS; i++)
            rtpmidi_set_callback(g_rtpmidi_js_ctx, i, NULL, NULL);
    }
    g_rtpmidi_js_ctx = rtp_ctx;
    if (g_rtpmidi_js_ctx)
    {
        for (int i = 0; i < MAX_RTPMIDI_CALLBACKS; i++)
        {
            if (g_rtpmidi_callbacks_active[i])
                rtpmidi_set_callback(g_rtpmidi_js_ctx, i,
                                     rtpmidi_input_callback, state);
        }
    }
}

void js_midi_poll(JsMidiContext *state)
{
    if (!state || !g_midi_js_jsctx)
        return;

    if (!JS_IsUndefined(g_midi_devices_changed_callback) && g_midi_ctx)
    {
        g_midi_device_check_frames++;
        uint64_t backend_generation = midi_get_device_generation(g_midi_ctx);
        bool backend_changed = backend_generation != g_midi_topology_observer.backend_generation;
        if (backend_changed || g_midi_device_check_frames >= MIDI_DEVICE_CHECK_FRAMES)
        {
            g_midi_device_check_frames = 0;
            midi_refresh_devices(g_midi_ctx);
            uint64_t fingerprint = midi_get_device_topology_fingerprint(g_midi_ctx);
            if (midi_topology_observer_update(&g_midi_topology_observer,
                                              fingerprint, backend_generation))
            {
                JSValue event = JS_NewObject(g_midi_js_jsctx);
                JS_SetPropertyStr(g_midi_js_jsctx, event, "inputs",
                                  js_midi_device_array(g_midi_js_jsctx, g_midi_ctx, true));
                JS_SetPropertyStr(g_midi_js_jsctx, event, "outputs",
                                  js_midi_device_array(g_midi_js_jsctx, g_midi_ctx, false));
                JS_SetPropertyStr(g_midi_js_jsctx, event, "generation",
                                  JS_NewUint32(g_midi_js_jsctx, g_midi_topology_observer.generation));
                JSValue args[1] = {event};
                JSValue result = JS_Call(g_midi_js_jsctx, g_midi_devices_changed_callback,
                                         JS_UNDEFINED, 1, args);
                JS_FreeValue(g_midi_js_jsctx, result);
                JS_FreeValue(g_midi_js_jsctx, event);
            }
        }
    }

    if (g_rtpmidi_js_ctx)
    {
        rtpmidi_poll(g_rtpmidi_js_ctx);
    }

    MidiQueuedMessage queued_message;
    while (midi_dequeue(state, &queued_message))
    {
        MidiQueuedMessage *qm = &queued_message;

        int handle = qm->device_handle;
        if (handle >= 0 && handle < MAX_MIDI_CALLBACKS && g_midi_callbacks_active[handle])
        {
            JSValue callback = g_midi_callbacks[handle];

            JSValue msg_obj = JS_NewObject(g_midi_js_jsctx);
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "status", JS_NewInt32(g_midi_js_jsctx, qm->message.status));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data1", JS_NewInt32(g_midi_js_jsctx, qm->message.data1));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data2", JS_NewInt32(g_midi_js_jsctx, qm->message.data2));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "timestamp", JS_NewFloat64(g_midi_js_jsctx, (double)qm->message.timestamp));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "type", JS_NewInt32(g_midi_js_jsctx, midi_get_type(qm->message.status)));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "channel", JS_NewInt32(g_midi_js_jsctx, midi_get_channel(qm->message.status)));

            JSValue args[1] = {msg_obj};
            JSValue ret = JS_Call(g_midi_js_jsctx, callback, JS_UNDEFINED, 1, args);
            JS_FreeValue(g_midi_js_jsctx, ret);
            JS_FreeValue(g_midi_js_jsctx, msg_obj);
        }
    }

    MidiQueuedSysEx queued_sysex;
    while (midi_sysex_dequeue(state, &queued_sysex))
    {
        MidiQueuedSysEx *sq = &queued_sysex;

        int handle = sq->device_handle;
        if (handle >= 0 && handle < MAX_MIDI_CALLBACKS && g_midi_callbacks_active[handle])
        {
            JSValue callback = g_midi_callbacks[handle];

            JSValue msg_obj = JS_NewObject(g_midi_js_jsctx);
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "status", JS_NewInt32(g_midi_js_jsctx, 0xF0));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data1", JS_NewInt32(g_midi_js_jsctx, 0));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data2", JS_NewInt32(g_midi_js_jsctx, 0));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "timestamp", JS_NewFloat64(g_midi_js_jsctx, 0));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "type", JS_NewInt32(g_midi_js_jsctx, 0xF0));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "channel", JS_NewInt32(g_midi_js_jsctx, -1));

            JSValue data_arr = JS_NewArray(g_midi_js_jsctx);
            for (size_t i = 0; i < sq->length; i++)
            {
                JS_SetPropertyUint32(g_midi_js_jsctx, data_arr, i, JS_NewInt32(g_midi_js_jsctx, sq->data[i]));
            }
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data", data_arr);

            JSValue args[1] = {msg_obj};
            JSValue ret = JS_Call(g_midi_js_jsctx, callback, JS_UNDEFINED, 1, args);
            JS_FreeValue(g_midi_js_jsctx, ret);
            JS_FreeValue(g_midi_js_jsctx, msg_obj);
        }
    }

    RtpMidiQueuedMessage queued_rtpmidi;
    while (subsystem_queue_try_pop(&state->rtpmidi_queue, &queued_rtpmidi))
    {
        RtpMidiQueuedMessage *rm = &queued_rtpmidi;
        int session = rm->session_handle;

        if (session >= 0 && session < MAX_RTPMIDI_CALLBACKS && g_rtpmidi_callbacks_active[session])
        {
            JSValue callback = g_rtpmidi_callbacks[session];

            JSValue msg_obj = JS_NewObject(g_midi_js_jsctx);
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "status", JS_NewInt32(g_midi_js_jsctx, rm->message.status));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data1", JS_NewInt32(g_midi_js_jsctx, rm->message.data1));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "data2", JS_NewInt32(g_midi_js_jsctx, rm->message.data2));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "timestamp", JS_NewFloat64(g_midi_js_jsctx, (double)rm->message.timestamp));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "type", JS_NewInt32(g_midi_js_jsctx, midi_get_type(rm->message.status)));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "channel", JS_NewInt32(g_midi_js_jsctx, midi_get_channel(rm->message.status)));
            JS_SetPropertyStr(g_midi_js_jsctx, msg_obj, "network", JS_TRUE);

            JSValue args[1] = {msg_obj};
            JSValue ret = JS_Call(g_midi_js_jsctx, callback, JS_UNDEFINED, 1, args);
            JS_FreeValue(g_midi_js_jsctx, ret);
            JS_FreeValue(g_midi_js_jsctx, msg_obj);
        }
    }
}

size_t js_midi_dropped_messages(JsMidiContext *state)
{
    return state ? subsystem_queue_dropped(&state->midi_queue) : 0;
}

size_t js_midi_dropped_sysex(JsMidiContext *state)
{
    return state ? subsystem_queue_dropped(&state->sysex_queue) : 0;
}

size_t js_midi_dropped_rtpmidi(JsMidiContext *state)
{
    return state ? subsystem_queue_dropped(&state->rtpmidi_queue) : 0;
}

#undef JS_MIDI_STATE
#undef JS_MIDI_CALLBACK