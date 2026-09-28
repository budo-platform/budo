#ifndef LUA_MIDI_BINDINGS_H
#define LUA_MIDI_BINDINGS_H

#include <stdbool.h>
#include "midi_wrapper.h"
#include "rtpmidi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LuaMidiContext LuaMidiContext;

    LuaMidiContext *lua_midi_init(void *L);

    void lua_midi_cleanup(LuaMidiContext *state);

    void lua_midi_poll(LuaMidiContext *state);

    void lua_midi_set_rtpmidi(LuaMidiContext *state,
                              RtpMidiContext *rtp_ctx);

    size_t lua_midi_dropped_messages(LuaMidiContext *state);
    size_t lua_midi_dropped_sysex(LuaMidiContext *state);
    size_t lua_midi_dropped_rtpmidi(LuaMidiContext *state);

#ifdef __cplusplus
}
#endif

#endif