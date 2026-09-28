#include "lua_audio_bindings.h"
#include "audio_service.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

struct LuaAudioContext
{
    AudioContext *audio_ctx;
    bool lazy_initialized;
    char asset_root[4096];
};

static AudioContext *lua_audio_context(lua_State *L)
{
    LuaAudioContext *state =
        (LuaAudioContext *)lua_touserdata(L, lua_upvalueindex(1));
    if (!state)
        return NULL;
    if (!state->lazy_initialized)
    {
        state->lazy_initialized = true;
        state->audio_ctx = audio_create();
        if (state->audio_ctx)
            audio_set_asset_root(state->audio_ctx, state->asset_root);
        else
            fprintf(stderr, "Warning: Failed to create Lua audio context\n");
    }
    return state->audio_ctx;
}

#define ensure_lua_audio_ctx() AudioContext *g_lua_audio_ctx = lua_audio_context(L)

static int l_audio_start(lua_State *L)
{
    (void)L;
    ensure_lua_audio_ctx();
    if (g_lua_audio_ctx)
        audio_start(g_lua_audio_ctx);
    return 0;
}

static int l_audio_stop(lua_State *L)
{
    (void)L;
    ensure_lua_audio_ctx();
    if (g_lua_audio_ctx)
        audio_stop(g_lua_audio_ctx);
    return 0;
}

static int l_audio_is_playing(lua_State *L)
{
    ensure_lua_audio_ctx();
    lua_pushboolean(L, g_lua_audio_ctx && audio_is_playing(g_lua_audio_ctx));
    return 1;
}

static int l_audio_set_master_gain(lua_State *L)
{
    ensure_lua_audio_ctx();
    float gain = (float)luaL_checknumber(L, 1);
    if (g_lua_audio_ctx)
        audio_set_master_gain(g_lua_audio_ctx, gain);
    return 0;
}

static int l_audio_get_master_gain(lua_State *L)
{
    ensure_lua_audio_ctx();
    float gain = g_lua_audio_ctx ? audio_get_master_gain(g_lua_audio_ctx) : 0.5f;
    lua_pushnumber(L, gain);
    return 1;
}

static int l_audio_create_oscillator(lua_State *L)
{
    ensure_lua_audio_ctx();
    ApiError error;
    int id = audio_service_create_oscillator(g_lua_audio_ctx, &error);
    lua_pushinteger(L, id);
    return 1;
}

static int l_audio_destroy_oscillator(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_destroy_oscillator(g_lua_audio_ctx, id);
    return 0;
}

static int l_audio_set_oscillator_type(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    AudioWaveType type = AUDIO_WAVE_SINE;

    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        type = (AudioWaveType)(int)lua_tointeger(L, 2);
    }
    else if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *s = lua_tostring(L, 2);
        audio_wave_type_from_string(s, &type);
    }

    if (g_lua_audio_ctx)
        audio_oscillator_set_type(g_lua_audio_ctx, id, type);
    return 0;
}

static int l_audio_set_oscillator_frequency(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    float freq = (float)luaL_checknumber(L, 2);
    if (g_lua_audio_ctx)
        audio_oscillator_set_frequency(g_lua_audio_ctx, id, freq);
    return 0;
}

static int l_audio_set_oscillator_gain(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    float gain = (float)luaL_checknumber(L, 2);
    if (g_lua_audio_ctx)
        audio_oscillator_set_gain(g_lua_audio_ctx, id, gain);
    return 0;
}

static int l_audio_set_oscillator_detune(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    float cents = (float)luaL_checknumber(L, 2);
    if (g_lua_audio_ctx)
        audio_oscillator_set_detune(g_lua_audio_ctx, id, cents);
    return 0;
}

static int l_audio_start_oscillator(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_oscillator_start(g_lua_audio_ctx, id);
    return 0;
}

static int l_audio_stop_oscillator(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_oscillator_stop(g_lua_audio_ctx, id);
    return 0;
}

static int l_audio_set_oscillator_envelope(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    float attack = (float)luaL_checknumber(L, 2);
    float decay = (float)luaL_checknumber(L, 3);
    float sustain = (float)luaL_checknumber(L, 4);
    float release = (float)luaL_checknumber(L, 5);
    if (g_lua_audio_ctx)
        audio_oscillator_set_envelope(g_lua_audio_ctx, id, attack, decay, sustain, release);
    return 0;
}

static int l_audio_note_on(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_oscillator_note_on(g_lua_audio_ctx, id);
    return 0;
}

static int l_audio_note_off(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_oscillator_note_off(g_lua_audio_ctx, id);
    return 0;
}

static int l_audio_create_buffer(lua_State *L)
{
    ensure_lua_audio_ctx();
    int sample_rate = (int)luaL_checkinteger(L, 1);
    int channels = (int)luaL_checkinteger(L, 2);
    int num_samples = (int)luaL_checkinteger(L, 3);
    int id = g_lua_audio_ctx
                 ? audio_create_buffer(g_lua_audio_ctx, sample_rate, channels, num_samples)
                 : -1;
    lua_pushinteger(L, id);
    return 1;
}

static int l_audio_load_buffer(lua_State *L)
{
    ensure_lua_audio_ctx();
    const char *path = luaL_checkstring(L, 1);
    int id = g_lua_audio_ctx ? audio_load_buffer(g_lua_audio_ctx, path) : -1;
    lua_pushinteger(L, id);
    return 1;
}

static int l_audio_load_buffer_from_buffer(lua_State *L)
{
    ensure_lua_audio_ctx();
    size_t len = 0;
    const char *data = luaL_checklstring(L, 1, &len);
    int id = g_lua_audio_ctx ? audio_load_buffer_from_memory(g_lua_audio_ctx, (const uint8_t *)data, len) : -1;
    lua_pushinteger(L, id);
    return 1;
}

static int l_audio_get_error(lua_State *L)
{
    ensure_lua_audio_ctx();
    lua_pushstring(L, g_lua_audio_ctx ? audio_get_error(g_lua_audio_ctx) : "Audio context unavailable");
    return 1;
}

static int l_audio_set_buffer_data(lua_State *L)
{
    ensure_lua_audio_ctx();
    if (!g_lua_audio_ctx)
        return 0;

    int buffer_id = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_Integer count = (lua_Integer)lua_rawlen(L, 2);
    int offset = 0;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
        offset = (int)luaL_checkinteger(L, 3);

    if (count <= 0)
        return 0;

    float *samples = (float *)malloc((size_t)count * sizeof(float));
    if (!samples)
        return luaL_error(L, "audio.setBufferData: out of memory");

    for (lua_Integer i = 0; i < count; i++)
    {
        lua_rawgeti(L, 2, i + 1); 
        samples[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    audio_buffer_set_data(g_lua_audio_ctx, buffer_id, samples, offset, (int)count);
    free(samples);
    return 0;
}

static int l_audio_destroy_buffer(lua_State *L)
{
    ensure_lua_audio_ctx();
    int id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_destroy_buffer(g_lua_audio_ctx, id);
    return 0;
}

static int l_audio_play_buffer(lua_State *L)
{
    ensure_lua_audio_ctx();
    int buffer_id = (int)luaL_checkinteger(L, 1);
    bool loop = lua_toboolean(L, 2) != 0;
    float gain = (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
                     ? (float)lua_tonumber(L, 3)
                     : 1.0f;
    int id = g_lua_audio_ctx
                 ? audio_play_buffer(g_lua_audio_ctx, buffer_id, loop, gain)
                 : -1;
    lua_pushinteger(L, id);
    return 1;
}

static int l_audio_stop_buffer(lua_State *L)
{
    ensure_lua_audio_ctx();
    int playback_id = (int)luaL_checkinteger(L, 1);
    if (g_lua_audio_ctx)
        audio_stop_buffer(g_lua_audio_ctx, playback_id);
    return 0;
}

static int l_audio_midi_to_freq(lua_State *L)
{
    int note = (int)luaL_checkinteger(L, 1);
    lua_pushnumber(L, audio_midi_to_freq(note));
    return 1;
}

static const luaL_Reg audio_funcs[] = {
    
    {"start", l_audio_start},
    {"stop", l_audio_stop},
    {"isPlaying", l_audio_is_playing},
    {"setMasterGain", l_audio_set_master_gain},
    {"getMasterGain", l_audio_get_master_gain},
    
    {"createOscillator", l_audio_create_oscillator},
    {"destroyOscillator", l_audio_destroy_oscillator},
    {"setOscillatorType", l_audio_set_oscillator_type},
    {"setOscillatorFrequency", l_audio_set_oscillator_frequency},
    {"setOscillatorGain", l_audio_set_oscillator_gain},
    {"setOscillatorDetune", l_audio_set_oscillator_detune},
    {"startOscillator", l_audio_start_oscillator},
    {"stopOscillator", l_audio_stop_oscillator},
    {"setOscillatorEnvelope", l_audio_set_oscillator_envelope},
    {"noteOn", l_audio_note_on},
    {"noteOff", l_audio_note_off},
    
    {"loadBuffer", l_audio_load_buffer},
    {"loadBufferFromBuffer", l_audio_load_buffer_from_buffer},
    {"createBuffer", l_audio_create_buffer},
    {"setBufferData", l_audio_set_buffer_data},
    {"destroyBuffer", l_audio_destroy_buffer},
    {"playBuffer", l_audio_play_buffer},
    {"stopBuffer", l_audio_stop_buffer},
    
    {"midiToFreq", l_audio_midi_to_freq},
    {"getError", l_audio_get_error},
    {NULL, NULL}};

LuaAudioContext *lua_audio_init(void *L_void, const char *asset_root)
{
    lua_State *L = (lua_State *)L_void;
    LuaAudioContext *state =
        (LuaAudioContext *)calloc(1, sizeof(LuaAudioContext));
    if (!state)
        return NULL;

    snprintf(state->asset_root, sizeof(state->asset_root), "%s",
             asset_root ? asset_root : ".");

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, state);
    luaL_setfuncs(L, audio_funcs, 1);

    lua_pushinteger(L, AUDIO_WAVE_SINE);
    lua_setfield(L, -2, "SINE");
    lua_pushinteger(L, AUDIO_WAVE_SQUARE);
    lua_setfield(L, -2, "SQUARE");
    lua_pushinteger(L, AUDIO_WAVE_SAWTOOTH);
    lua_setfield(L, -2, "SAWTOOTH");
    lua_pushinteger(L, AUDIO_WAVE_TRIANGLE);
    lua_setfield(L, -2, "TRIANGLE");
    lua_pushinteger(L, AUDIO_WAVE_NOISE);
    lua_setfield(L, -2, "NOISE");

    lua_setfield(L, -2, "audio");
    lua_pop(L, 1); 

    return state;
}

void lua_audio_cleanup(LuaAudioContext *state)
{
    if (!state)
        return;
    audio_destroy(state->audio_ctx);
    free(state);
}

#undef ensure_lua_audio_ctx