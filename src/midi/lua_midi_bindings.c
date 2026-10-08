#include "midi/midi_event_queue.h"
#include "lua_midi_bindings.h"
#include "midi_service.h"
#include "midi_topology.h"
#include "core/subsystem_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#define LUA_MIDI_MSG_QUEUE_SIZE 1024
#define LUA_MIDI_SYSEX_QUEUE_SIZE 32
#define LUA_RTPMIDI_MSG_QUEUE_SIZE 256
#define LUA_MIDI_DEVICE_CHECK_FRAMES 60
#define LUA_MAX_MIDI_CALLBACKS MIDI_MAX_DEVICES
#define LUA_MAX_RTPMIDI_CALLBACKS RTPMIDI_MAX_SESSIONS

typedef struct
{
    int session_handle;
    MidiMessage message;
} LuaRtpMidiQueuedMessage;

struct LuaMidiContext
{
    lua_State *lua_state;
    MidiContext *midi_ctx;
    RtpMidiContext *rtpmidi_ctx;
    bool midi_lazy_initialized;
    MidiEventQueue *events; 
    uint8_t *sysex_buffer;  
    SubsystemQueue rtpmidi_queue;
    LuaRtpMidiQueuedMessage rtpmidi_storage[LUA_RTPMIDI_MSG_QUEUE_SIZE];
    int midi_callbacks[LUA_MAX_MIDI_CALLBACKS];
    bool midi_callbacks_active[LUA_MAX_MIDI_CALLBACKS];
    int rtpmidi_callbacks[LUA_MAX_RTPMIDI_CALLBACKS];
    bool rtpmidi_callbacks_active[LUA_MAX_RTPMIDI_CALLBACKS];
    int devices_changed_callback;
    MidiTopologyObserver topology_observer;
    int device_check_frames;
};

static LuaMidiContext *lua_midi_binding_state(lua_State *L)
{
    return (LuaMidiContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static MidiContext *lua_midi_context(LuaMidiContext *state)
{
    if (!state)
        return NULL;
    if (!state->midi_lazy_initialized)
    {
        state->midi_lazy_initialized = true;
        state->midi_ctx = midi_create();
        if (!state->midi_ctx)
            fprintf(stderr, "Warning: Failed to create Lua MIDI context\n");
    }
    return state->midi_ctx;
}

#define LUA_MIDI_STATE() LuaMidiContext *state = lua_midi_binding_state(L)
#define ensure_lua_midi_ctx() ((void)lua_midi_context(state))

static void lua_midi_input_callback(int handle, const MidiMessage *message, void *user_data)
{
    LuaMidiContext *state = (LuaMidiContext *)user_data;
    if (state && message)
        midi_event_queue_push_message(state->events, handle, message);
}

static void lua_midi_sysex_callback(int handle, const uint8_t *data, size_t length, void *user_data)
{
    LuaMidiContext *state = (LuaMidiContext *)user_data;
    if (state && data)
        midi_event_queue_push_sysex(state->events, handle, data, length, 0);
}

static void lua_rtpmidi_input_callback(int session_handle, const MidiMessage *message, void *user_data)
{
    LuaMidiContext *state = (LuaMidiContext *)user_data;
    LuaRtpMidiQueuedMessage queued;
    if (!state || !message)
        return;
    queued.session_handle = session_handle;
    queued.message = *message;
    subsystem_queue_try_push(&state->rtpmidi_queue, &queued);
}

static void push_midi_message(lua_State *L, const MidiMessage *msg)
{
    lua_newtable(L);
    lua_pushinteger(L, msg->status);
    lua_setfield(L, -2, "status");
    lua_pushinteger(L, msg->data1);
    lua_setfield(L, -2, "data1");
    lua_pushinteger(L, msg->data2);
    lua_setfield(L, -2, "data2");
    lua_pushnumber(L, (lua_Number)msg->timestamp);
    lua_setfield(L, -2, "timestamp");
    lua_pushinteger(L, midi_get_type(msg->status));
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, midi_get_channel(msg->status));
    lua_setfield(L, -2, "channel");
}

static int l_midi_get_input_count(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    lua_pushinteger(L, state->midi_ctx ? midi_get_input_count(state->midi_ctx) : 0);
    return 1;
}

static int l_midi_get_output_count(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    lua_pushinteger(L, state->midi_ctx ? midi_get_output_count(state->midi_ctx) : 0);
    return 1;
}

static int l_midi_get_input_devices(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    lua_newtable(L);
    if (!state->midi_ctx)
        return 1;

    int count = midi_get_input_count(state->midi_ctx);
    for (int i = 0; i < count; i++)
    {
        MidiDeviceInfo info;
        if (midi_get_input_info(state->midi_ctx, i, &info))
        {
            lua_newtable(L);
            lua_pushinteger(L, info.id);
            lua_setfield(L, -2, "id");
            lua_pushstring(L, info.name);
            lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, i + 1);
        }
    }
    return 1;
}

static int l_midi_get_output_devices(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    lua_newtable(L);
    if (!state->midi_ctx)
        return 1;

    int count = midi_get_output_count(state->midi_ctx);
    for (int i = 0; i < count; i++)
    {
        MidiDeviceInfo info;
        if (midi_get_output_info(state->midi_ctx, i, &info))
        {
            lua_newtable(L);
            lua_pushinteger(L, info.id);
            lua_setfield(L, -2, "id");
            lua_pushstring(L, info.name);
            lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, i + 1);
        }
    }
    return 1;
}

static int l_midi_refresh_devices(lua_State *L)
{
    LUA_MIDI_STATE();
    (void)L;
    ensure_lua_midi_ctx();
    if (state->midi_ctx)
        midi_refresh_devices(state->midi_ctx);
    return 0;
}

static int l_midi_on_devices_changed(lua_State *L)
{
    LUA_MIDI_STATE();
    luaL_checkany(L, 1);
    if (!lua_isfunction(L, 1) && !lua_isnil(L, 1))
        return luaL_error(L, "midi.onDevicesChanged expects a callback or nil");
    if (state->devices_changed_callback != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, state->devices_changed_callback);
    state->devices_changed_callback = LUA_NOREF;
    state->device_check_frames = 0;
    if (lua_isnil(L, 1))
        return 0;

    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
        return 0;
    midi_refresh_devices(state->midi_ctx);
    midi_topology_observer_reset(&state->topology_observer,
                                 midi_get_device_topology_fingerprint(state->midi_ctx),
                                 midi_get_device_generation(state->midi_ctx));
    lua_pushvalue(L, 1);
    state->devices_changed_callback = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static void lua_push_midi_devices(lua_State *L, MidiContext *ctx, bool inputs)
{
    lua_newtable(L);
    int count = inputs ? midi_get_input_count(ctx) : midi_get_output_count(ctx);
    for (int i = 0; i < count; i++)
    {
        MidiDeviceInfo info;
        bool ok = inputs ? midi_get_input_info(ctx, i, &info)
                         : midi_get_output_info(ctx, i, &info);
        if (ok)
        {
            lua_newtable(L);
            lua_pushinteger(L, info.id);
            lua_setfield(L, -2, "id");
            lua_pushstring(L, info.name);
            lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, i + 1);
        }
    }
}

static int l_midi_open_input(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    int device_index = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    int handle = midi_open_input(state->midi_ctx, device_index,
                                 lua_midi_input_callback, state);
    if (handle < 0)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    midi_set_sysex_callback(state->midi_ctx, handle,
                            lua_midi_sysex_callback, state);

    if (handle >= 0 && handle < LUA_MAX_MIDI_CALLBACKS)
    {
        if (state->midi_callbacks_active[handle])
            luaL_unref(L, LUA_REGISTRYINDEX, state->midi_callbacks[handle]);

        lua_pushvalue(L, 2);
        state->midi_callbacks[handle] = luaL_ref(L, LUA_REGISTRYINDEX);
        state->midi_callbacks_active[handle] = true;
    }

    lua_pushinteger(L, handle);
    return 1;
}

static int l_midi_close_input(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
        return 0;

    int handle = (int)luaL_checkinteger(L, 1);

    if (handle >= 0 && handle < LUA_MAX_MIDI_CALLBACKS && state->midi_callbacks_active[handle])
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->midi_callbacks[handle]);
        state->midi_callbacks_active[handle] = false;
    }

    midi_close_input(state->midi_ctx, handle);
    return 0;
}

static int l_midi_open_output(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }

    int device_index = (int)luaL_checkinteger(L, 1);
    ApiError error;
    lua_pushinteger(L, midi_service_open_output(state->midi_ctx, device_index,
                                                &error));
    return 1;
}

static int l_midi_close_output(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
        return 0;

    int handle = (int)luaL_checkinteger(L, 1);
    midi_close_output(state->midi_ctx, handle);
    return 0;
}

static int l_midi_send_message(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    int status = (int)luaL_checkinteger(L, 2);
    int data1 = (int)luaL_checkinteger(L, 3);
    int data2 = (int)luaL_checkinteger(L, 4);

    lua_pushboolean(L, midi_send_message(state->midi_ctx, handle, (uint8_t)status,
                                         (uint8_t)data1, (uint8_t)data2));
    return 1;
}

static int l_midi_send_raw(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    int handle = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    int len = (int)lua_rawlen(L, 2);
    if (len <= 0 || len > MIDI_SYSEX_MAX_SIZE)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    for (int i = 0; i < len; i++)
    {
        lua_rawgeti(L, 2, i + 1);
        buf[i] = (uint8_t)(lua_tointeger(L, -1) & 0xFF);
        lua_pop(L, 1);
    }

    bool result = midi_send_raw(state->midi_ctx, handle, buf, len);
    free(buf);

    lua_pushboolean(L, result);
    return 1;
}

static int l_midi_note_on(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int note = (int)luaL_checkinteger(L, 3);
    int velocity = (int)luaL_checkinteger(L, 4);
    lua_pushboolean(L, midi_note_on(state->midi_ctx, handle, channel, note, velocity));
    return 1;
}

static int l_midi_note_off(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int note = (int)luaL_checkinteger(L, 3);
    int velocity = (int)luaL_checkinteger(L, 4);
    lua_pushboolean(L, midi_note_off(state->midi_ctx, handle, channel, note, velocity));
    return 1;
}

static int l_midi_control_change(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int control = (int)luaL_checkinteger(L, 3);
    int value = (int)luaL_checkinteger(L, 4);
    lua_pushboolean(L, midi_control_change(state->midi_ctx, handle, channel, control, value));
    return 1;
}

static int l_midi_program_change(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int program = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, midi_program_change(state->midi_ctx, handle, channel, program));
    return 1;
}

static int l_midi_pitch_bend(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int value = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, midi_pitch_bend(state->midi_ctx, handle, channel, value));
    return 1;
}

static int l_midi_channel_pressure(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int pressure = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, midi_channel_pressure(state->midi_ctx, handle, channel, pressure));
    return 1;
}

static int l_midi_poly_pressure(lua_State *L)
{
    LUA_MIDI_STATE();
    ensure_lua_midi_ctx();
    if (!state->midi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int handle = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int note = (int)luaL_checkinteger(L, 3);
    int pressure = (int)luaL_checkinteger(L, 4);
    lua_pushboolean(L, midi_poly_pressure(state->midi_ctx, handle, channel, note, pressure));
    return 1;
}

static int l_midi_get_type(lua_State *L)
{
    int status = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, midi_get_type((uint8_t)status));
    return 1;
}

static int l_midi_get_channel(lua_State *L)
{
    int status = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, midi_get_channel((uint8_t)status));
    return 1;
}

static int l_midi_create_session(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushinteger(L, -1);
        return 1;
    }
    const char *name = luaL_checkstring(L, 1);
    int port = (int)luaL_optinteger(L, 2, 0);
    lua_pushinteger(L, rtpmidi_create_session(state->rtpmidi_ctx, name, port));
    return 1;
}

static int l_midi_connect_session(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int session = (int)luaL_checkinteger(L, 1);
    const char *host = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);
    lua_pushboolean(L, rtpmidi_connect(state->rtpmidi_ctx, session, host, port));
    return 1;
}

static int l_midi_destroy_session(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
        return 0;
    int session = (int)luaL_checkinteger(L, 1);

    if (session >= 0 && session < LUA_MAX_RTPMIDI_CALLBACKS && state->rtpmidi_callbacks_active[session])
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->rtpmidi_callbacks[session]);
        state->rtpmidi_callbacks_active[session] = false;
    }

    rtpmidi_destroy_session(state->rtpmidi_ctx, session);
    return 0;
}

static int l_midi_on_session_message(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
        return 0;

    int session = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (session < 0 || session >= LUA_MAX_RTPMIDI_CALLBACKS)
        return 0;

    if (state->rtpmidi_callbacks_active[session])
    {
        luaL_unref(L, LUA_REGISTRYINDEX, state->rtpmidi_callbacks[session]);
        state->rtpmidi_callbacks_active[session] = false;
    }

    lua_pushvalue(L, 2);
    state->rtpmidi_callbacks[session] = luaL_ref(L, LUA_REGISTRYINDEX);
    state->rtpmidi_callbacks_active[session] = true;

    rtpmidi_set_callback(state->rtpmidi_ctx, session,
                         lua_rtpmidi_input_callback, state);
    return 0;
}

static int l_midi_session_send(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int session = (int)luaL_checkinteger(L, 1);
    int status = (int)luaL_checkinteger(L, 2);
    int data1 = (int)luaL_checkinteger(L, 3);
    int data2 = (int)luaL_checkinteger(L, 4);
    lua_pushboolean(L, rtpmidi_send_message(state->rtpmidi_ctx, session,
                                            (uint8_t)status, (uint8_t)data1, (uint8_t)data2));
    return 1;
}

static int l_midi_session_note_on(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int session = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int note = (int)luaL_checkinteger(L, 3);
    int velocity = (int)luaL_checkinteger(L, 4);
    uint8_t status = MIDI_NOTE_ON | (channel & 0x0F);
    lua_pushboolean(L, rtpmidi_send_message(state->rtpmidi_ctx, session,
                                            status, (uint8_t)(note & 0x7F), (uint8_t)(velocity & 0x7F)));
    return 1;
}

static int l_midi_session_note_off(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int session = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int note = (int)luaL_checkinteger(L, 3);
    int velocity = (int)luaL_checkinteger(L, 4);
    uint8_t status = MIDI_NOTE_OFF | (channel & 0x0F);
    lua_pushboolean(L, rtpmidi_send_message(state->rtpmidi_ctx, session,
                                            status, (uint8_t)(note & 0x7F), (uint8_t)(velocity & 0x7F)));
    return 1;
}

static int l_midi_session_cc(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int session = (int)luaL_checkinteger(L, 1);
    int channel = (int)luaL_checkinteger(L, 2);
    int control = (int)luaL_checkinteger(L, 3);
    int value = (int)luaL_checkinteger(L, 4);
    uint8_t status = MIDI_CONTROL_CHANGE | (channel & 0x0F);
    lua_pushboolean(L, rtpmidi_send_message(state->rtpmidi_ctx, session,
                                            status, (uint8_t)(control & 0x7F), (uint8_t)(value & 0x7F)));
    return 1;
}

static int l_midi_session_send_raw(lua_State *L)
{
    LUA_MIDI_STATE();
    if (!state->rtpmidi_ctx)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    int session = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    int len = (int)lua_rawlen(L, 2);
    if (len <= 0 || len > 512)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    for (int i = 0; i < len; i++)
    {
        lua_rawgeti(L, 2, i + 1);
        buf[i] = (uint8_t)(lua_tointeger(L, -1) & 0xFF);
        lua_pop(L, 1);
    }

    bool result = rtpmidi_send_raw(state->rtpmidi_ctx, session, buf, len);
    free(buf);

    lua_pushboolean(L, result);
    return 1;
}

static int l_midi_get_sessions(lua_State *L)
{
    LUA_MIDI_STATE();
    lua_newtable(L);
    if (!state->rtpmidi_ctx)
        return 1;

    int count = rtpmidi_get_session_count(state->rtpmidi_ctx);
    for (int i = 0; i < count; i++)
    {
        RtpMidiSessionInfo info;
        if (rtpmidi_get_session_info(state->rtpmidi_ctx, i, &info))
        {
            lua_newtable(L);
            lua_pushinteger(L, info.handle);
            lua_setfield(L, -2, "handle");
            lua_pushstring(L, info.name);
            lua_setfield(L, -2, "name");
            lua_pushinteger(L, info.port);
            lua_setfield(L, -2, "port");

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
            lua_pushstring(L, state_str);
            lua_setfield(L, -2, "state");
            lua_pushinteger(L, info.peer_count);
            lua_setfield(L, -2, "peerCount");

            lua_rawseti(L, -2, i + 1);
        }
    }
    return 1;
}

static const luaL_Reg midi_funcs[] = {
    
    {"getInputCount", l_midi_get_input_count},
    {"getOutputCount", l_midi_get_output_count},
    {"getInputDevices", l_midi_get_input_devices},
    {"getOutputDevices", l_midi_get_output_devices},
    {"refreshDevices", l_midi_refresh_devices},
    {"onDevicesChanged", l_midi_on_devices_changed},
    
    {"openInput", l_midi_open_input},
    {"closeInput", l_midi_close_input},
    
    {"openOutput", l_midi_open_output},
    {"closeOutput", l_midi_close_output},
    {"sendMessage", l_midi_send_message},
    {"sendRaw", l_midi_send_raw},
    
    {"noteOn", l_midi_note_on},
    {"noteOff", l_midi_note_off},
    {"controlChange", l_midi_control_change},
    {"programChange", l_midi_program_change},
    {"pitchBend", l_midi_pitch_bend},
    {"channelPressure", l_midi_channel_pressure},
    {"polyPressure", l_midi_poly_pressure},
    
    {"getType", l_midi_get_type},
    {"getChannel", l_midi_get_channel},
    
    {"createSession", l_midi_create_session},
    {"connectSession", l_midi_connect_session},
    {"destroySession", l_midi_destroy_session},
    {"onSessionMessage", l_midi_on_session_message},
    {"sessionSend", l_midi_session_send},
    {"sessionNoteOn", l_midi_session_note_on},
    {"sessionNoteOff", l_midi_session_note_off},
    {"sessionControlChange", l_midi_session_cc},
    {"sessionSendRaw", l_midi_session_send_raw},
    {"getSessions", l_midi_get_sessions},
    {NULL, NULL}};

LuaMidiContext *lua_midi_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    LuaMidiContext *state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->lua_state = L;
    state->devices_changed_callback = LUA_NOREF;
    state->events = midi_event_queue_create(LUA_MIDI_MSG_QUEUE_SIZE, LUA_MIDI_SYSEX_QUEUE_SIZE);
    state->sysex_buffer = (uint8_t *)malloc(MIDI_SYSEX_MAX_SIZE);
    if (!state->events || !state->sysex_buffer ||
        !subsystem_queue_init(&state->rtpmidi_queue, state->rtpmidi_storage,
                              sizeof(state->rtpmidi_storage[0]), LUA_RTPMIDI_MSG_QUEUE_SIZE))
    {
        subsystem_queue_destroy(&state->rtpmidi_queue);
        midi_event_queue_destroy(state->events);
        free(state->sysex_buffer);
        free(state);
        return NULL;
    }

    for (int i = 0; i < LUA_MAX_MIDI_CALLBACKS; i++)
    {
        state->midi_callbacks[i] = LUA_NOREF;
        state->midi_callbacks_active[i] = false;
    }
    for (int i = 0; i < LUA_MAX_RTPMIDI_CALLBACKS; i++)
    {
        state->rtpmidi_callbacks[i] = LUA_NOREF;
        state->rtpmidi_callbacks_active[i] = false;
    }

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, state);
    luaL_setfuncs(L, midi_funcs, 1);

    lua_pushinteger(L, MIDI_NOTE_OFF);
    lua_setfield(L, -2, "NOTE_OFF");
    lua_pushinteger(L, MIDI_NOTE_ON);
    lua_setfield(L, -2, "NOTE_ON");
    lua_pushinteger(L, MIDI_POLY_PRESSURE);
    lua_setfield(L, -2, "POLY_PRESSURE");
    lua_pushinteger(L, MIDI_CONTROL_CHANGE);
    lua_setfield(L, -2, "CONTROL_CHANGE");
    lua_pushinteger(L, MIDI_PROGRAM_CHANGE);
    lua_setfield(L, -2, "PROGRAM_CHANGE");
    lua_pushinteger(L, MIDI_CHANNEL_PRESSURE);
    lua_setfield(L, -2, "CHANNEL_PRESSURE");
    lua_pushinteger(L, MIDI_PITCH_BEND);
    lua_setfield(L, -2, "PITCH_BEND");
    lua_pushinteger(L, 0xF0);
    lua_setfield(L, -2, "SYSEX");

    lua_pushinteger(L, MIDI_CC_MOD_WHEEL);
    lua_setfield(L, -2, "CC_MOD_WHEEL");
    lua_pushinteger(L, MIDI_CC_BREATH);
    lua_setfield(L, -2, "CC_BREATH");
    lua_pushinteger(L, MIDI_CC_FOOT);
    lua_setfield(L, -2, "CC_FOOT");
    lua_pushinteger(L, MIDI_CC_VOLUME);
    lua_setfield(L, -2, "CC_VOLUME");
    lua_pushinteger(L, MIDI_CC_PAN);
    lua_setfield(L, -2, "CC_PAN");
    lua_pushinteger(L, MIDI_CC_EXPRESSION);
    lua_setfield(L, -2, "CC_EXPRESSION");
    lua_pushinteger(L, MIDI_CC_SUSTAIN);
    lua_setfield(L, -2, "CC_SUSTAIN");
    lua_pushinteger(L, MIDI_CC_ALL_SOUND_OFF);
    lua_setfield(L, -2, "CC_ALL_SOUND_OFF");
    lua_pushinteger(L, MIDI_CC_ALL_NOTES_OFF);
    lua_setfield(L, -2, "CC_ALL_NOTES_OFF");

    lua_setfield(L, -2, "midi");
    lua_pop(L, 1); 

    return state;
}

void lua_midi_cleanup(LuaMidiContext *state)
{
    if (!state)
        return;

    subsystem_queue_close(&state->rtpmidi_queue);

    if (state->rtpmidi_ctx)
    {
        for (int i = 0; i < LUA_MAX_RTPMIDI_CALLBACKS; i++)
            rtpmidi_set_callback(state->rtpmidi_ctx, i, NULL, NULL);
    }

    if (state->midi_ctx)
    {
        midi_destroy(state->midi_ctx);
        state->midi_ctx = NULL;
    }

    if (state->lua_state)
    {
        for (int i = 0; i < LUA_MAX_MIDI_CALLBACKS; i++)
        {
            if (state->midi_callbacks_active[i])
            {
                luaL_unref(state->lua_state, LUA_REGISTRYINDEX, state->midi_callbacks[i]);
                state->midi_callbacks_active[i] = false;
            }
        }
        for (int i = 0; i < LUA_MAX_RTPMIDI_CALLBACKS; i++)
        {
            if (state->rtpmidi_callbacks_active[i])
            {
                luaL_unref(state->lua_state, LUA_REGISTRYINDEX, state->rtpmidi_callbacks[i]);
                state->rtpmidi_callbacks_active[i] = false;
            }
        }
        if (state->devices_changed_callback != LUA_NOREF)
        {
            luaL_unref(state->lua_state, LUA_REGISTRYINDEX, state->devices_changed_callback);
            state->devices_changed_callback = LUA_NOREF;
        }
    }

    state->lua_state = NULL;
    state->rtpmidi_ctx = NULL;
    subsystem_queue_destroy(&state->rtpmidi_queue);
    midi_event_queue_destroy(state->events);
    state->events = NULL;
    free(state->sysex_buffer);
    state->sysex_buffer = NULL;
    free(state);
}

void lua_midi_set_rtpmidi(LuaMidiContext *state,
                          RtpMidiContext *rtp_ctx)
{
    if (!state)
        return;
    if (state->rtpmidi_ctx && state->rtpmidi_ctx != rtp_ctx)
    {
        for (int i = 0; i < LUA_MAX_RTPMIDI_CALLBACKS; i++)
            rtpmidi_set_callback(state->rtpmidi_ctx, i, NULL, NULL);
    }
    state->rtpmidi_ctx = rtp_ctx;
    if (state->rtpmidi_ctx)
    {
        for (int i = 0; i < LUA_MAX_RTPMIDI_CALLBACKS; i++)
        {
            if (state->rtpmidi_callbacks_active[i])
                rtpmidi_set_callback(state->rtpmidi_ctx, i,
                                     lua_rtpmidi_input_callback, state);
        }
    }
}

bool lua_midi_has_pending_work(LuaMidiContext *state)
{
    if (!state)
        return false;
    for (int i = 0; i < LUA_MAX_MIDI_CALLBACKS; i++)
    {
        if (state->midi_callbacks_active[i])
            return true;
    }
    for (int i = 0; i < LUA_MAX_RTPMIDI_CALLBACKS; i++)
    {
        if (state->rtpmidi_callbacks_active[i])
            return true;
    }
    return state->devices_changed_callback != LUA_NOREF;
}

void lua_midi_poll(LuaMidiContext *state)
{
    if (!state || !state->lua_state)
        return;

    if (state->devices_changed_callback != LUA_NOREF && state->midi_ctx)
    {
        state->device_check_frames++;
        uint64_t backend_generation = midi_get_device_generation(state->midi_ctx);
        bool backend_changed = backend_generation != state->topology_observer.backend_generation;
        if (backend_changed || state->device_check_frames >= LUA_MIDI_DEVICE_CHECK_FRAMES)
        {
            state->device_check_frames = 0;
            midi_refresh_devices(state->midi_ctx);
            uint64_t fingerprint = midi_get_device_topology_fingerprint(state->midi_ctx);
            if (midi_topology_observer_update(&state->topology_observer,
                                              fingerprint, backend_generation))
            {
                lua_rawgeti(state->lua_state, LUA_REGISTRYINDEX, state->devices_changed_callback);
                lua_newtable(state->lua_state);
                lua_push_midi_devices(state->lua_state, state->midi_ctx, true);
                lua_setfield(state->lua_state, -2, "inputs");
                lua_push_midi_devices(state->lua_state, state->midi_ctx, false);
                lua_setfield(state->lua_state, -2, "outputs");
                lua_pushinteger(state->lua_state, (lua_Integer)state->topology_observer.generation);
                lua_setfield(state->lua_state, -2, "generation");
                if (lua_pcall(state->lua_state, 1, 0, 0) != LUA_OK)
                {
                    fprintf(stderr, "Lua MIDI device callback error: %s\n", lua_tostring(state->lua_state, -1));
                    lua_pop(state->lua_state, 1);
                }
            }
        }
    }

    if (state->rtpmidi_ctx)
        rtpmidi_poll(state->rtpmidi_ctx);

    MidiEvent event;
    while (midi_event_queue_pop(state->events, &event, state->sysex_buffer))
    {
        int handle = event.handle;
        if (handle < 0 || handle >= LUA_MAX_MIDI_CALLBACKS || !state->midi_callbacks_active[handle])
            continue;
        lua_rawgeti(state->lua_state, LUA_REGISTRYINDEX, state->midi_callbacks[handle]);
        if (event.is_sysex)
        {
            lua_newtable(state->lua_state);
            lua_pushinteger(state->lua_state, 0xF0);
            lua_setfield(state->lua_state, -2, "status");
            lua_pushinteger(state->lua_state, 0);
            lua_setfield(state->lua_state, -2, "data1");
            lua_pushinteger(state->lua_state, 0);
            lua_setfield(state->lua_state, -2, "data2");
            lua_pushnumber(state->lua_state, (lua_Number)event.message.timestamp);
            lua_setfield(state->lua_state, -2, "timestamp");
            lua_pushinteger(state->lua_state, 0xF0);
            lua_setfield(state->lua_state, -2, "type");
            lua_pushinteger(state->lua_state, -1);
            lua_setfield(state->lua_state, -2, "channel");
            lua_newtable(state->lua_state);
            for (size_t i = 0; i < event.sysex_length; i++)
            {
                lua_pushinteger(state->lua_state, state->sysex_buffer[i]);
                lua_rawseti(state->lua_state, -2, (int)(i + 1));
            }
            lua_setfield(state->lua_state, -2, "data");
        }
        else
        {
            push_midi_message(state->lua_state, &event.message);
        }
        if (lua_pcall(state->lua_state, 1, 0, 0) != LUA_OK)
        {
            fprintf(stderr, "Lua MIDI callback error: %s\n", lua_tostring(state->lua_state, -1));
            lua_pop(state->lua_state, 1);
        }
    }

    LuaRtpMidiQueuedMessage queued_rtpmidi;
    while (subsystem_queue_try_pop(&state->rtpmidi_queue, &queued_rtpmidi))
    {
        LuaRtpMidiQueuedMessage *rm = &queued_rtpmidi;
        int session = rm->session_handle;

        if (session >= 0 && session < LUA_MAX_RTPMIDI_CALLBACKS && state->rtpmidi_callbacks_active[session])
        {
            lua_rawgeti(state->lua_state, LUA_REGISTRYINDEX, state->rtpmidi_callbacks[session]);
            push_midi_message(state->lua_state, &rm->message);
            lua_pushboolean(state->lua_state, 1);
            lua_setfield(state->lua_state, -2, "network");

            if (lua_pcall(state->lua_state, 1, 0, 0) != LUA_OK)
            {
                fprintf(stderr, "Lua RTP-MIDI callback error: %s\n", lua_tostring(state->lua_state, -1));
                lua_pop(state->lua_state, 1);
            }
        }
    }
}

size_t lua_midi_dropped_messages(LuaMidiContext *state)
{
    return state ? midi_event_queue_dropped_messages(state->events) : 0;
}

size_t lua_midi_dropped_sysex(LuaMidiContext *state)
{
    return state ? midi_event_queue_dropped_sysex(state->events) : 0;
}

size_t lua_midi_dropped_rtpmidi(LuaMidiContext *state)
{
    return state ? subsystem_queue_dropped(&state->rtpmidi_queue) : 0;
}

#undef LUA_MIDI_STATE