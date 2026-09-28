#ifndef LUA_AUDIO_BINDINGS_H
#define LUA_AUDIO_BINDINGS_H

#include "audio_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LuaAudioContext LuaAudioContext;

    LuaAudioContext *lua_audio_init(void *L, const char *asset_root);

    void lua_audio_cleanup(LuaAudioContext *state);

#ifdef __cplusplus
}
#endif

#endif