#ifndef BUDO_MANAGED_SUBSYSTEM_ADAPTERS_H
#define BUDO_MANAGED_SUBSYSTEM_ADAPTERS_H

#include "audio/js_audio_bindings.h"
#include "audio/lua_audio_bindings.h"
#include "device/device_service.h"
#include "file/js_file_bindings.h"
#include "file/lua_file_bindings.h"
#include "graphics/js_canvas_bindings.h"
#include "graphics/lua_canvas_bindings.h"
#ifdef BUDO_LLAMACPP
#include "llamacpp/js_llamacpp_bindings.h"
#include "llamacpp/lua_llamacpp_bindings.h"
#endif
#include "magneto/js_magneto_bindings.h"
#include "magneto/lua_magneto_bindings.h"
#include "midi/js_midi_bindings.h"
#include "midi/lua_midi_bindings.h"
#include "network/js_network_bindings.h"
#include "network/lua_network_bindings.h"
#include "sqlite/js_sqlite_bindings.h"
#include "sqlite/lua_sqlite_bindings.h"

#define BUDO_DEFINE_CLEANUP_ADAPTER(name, type, cleanup) \
    static inline void name(void *opaque)                \
    {                                                    \
        cleanup((type *)opaque);                         \
    }

#define BUDO_DEFINE_POLL_ADAPTER(name, type, poll) \
    static inline void name(void *opaque)          \
    {                                              \
        poll((type *)opaque);                      \
    }

BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_core_cleanup,
                            JSRuntimeContext, js_runtime_destroy)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_canvas_cleanup,
                            JSGraphicContext, js_graphic_destroy)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_canvas_cleanup,
                            LuaCanvasContext, lua_canvas_destroy)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_audio_cleanup,
                            JsAudioContext, js_audio_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_audio_cleanup,
                            LuaAudioContext, lua_audio_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_midi_cleanup,
                            JsMidiContext, js_midi_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_midi_cleanup,
                            LuaMidiContext, lua_midi_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_sqlite_cleanup,
                            SqliteContext, js_sqlite_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_sqlite_cleanup,
                            SqliteContext, lua_sqlite_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_file_cleanup,
                            JsFileContext, js_file_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_file_cleanup,
                            FileContext, lua_file_cleanup)
#ifdef BUDO_LLAMACPP
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_llamacpp_cleanup,
                            JsLlamaCppContext, js_llamacpp_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_llamacpp_cleanup,
                            LuaLlamaCppContext, lua_llamacpp_cleanup)
#endif
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_network_cleanup,
                            JsNetworkContext, js_network_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_network_cleanup,
                            LuaNetworkContext, lua_network_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_js_magneto_cleanup,
                            MagnetoContext, js_magneto_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_lua_magneto_cleanup,
                            MagnetoContext, lua_magneto_cleanup)
BUDO_DEFINE_CLEANUP_ADAPTER(managed_device_cleanup,
                            DeviceContext, device_binding_state_destroy)

BUDO_DEFINE_POLL_ADAPTER(managed_js_midi_poll,
                         JsMidiContext, js_midi_poll)
BUDO_DEFINE_POLL_ADAPTER(managed_lua_midi_poll,
                         LuaMidiContext, lua_midi_poll)
BUDO_DEFINE_POLL_ADAPTER(managed_js_file_poll,
                         JsFileContext, js_file_poll)
#ifdef BUDO_LLAMACPP
BUDO_DEFINE_POLL_ADAPTER(managed_js_llamacpp_poll,
                         JsLlamaCppContext, js_llamacpp_poll)
BUDO_DEFINE_POLL_ADAPTER(managed_lua_llamacpp_poll,
                         LuaLlamaCppContext, lua_llamacpp_poll)
#endif
BUDO_DEFINE_POLL_ADAPTER(managed_js_network_poll,
                         JsNetworkContext, js_network_poll)
BUDO_DEFINE_POLL_ADAPTER(managed_lua_network_poll,
                         LuaNetworkContext, lua_network_poll)

#undef BUDO_DEFINE_POLL_ADAPTER
#undef BUDO_DEFINE_CLEANUP_ADAPTER

#endif