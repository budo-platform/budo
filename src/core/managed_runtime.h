#ifndef BUDO_MANAGED_RUNTIME_H
#define BUDO_MANAGED_RUNTIME_H

#include "core/input.h"
#include "core/subsystem_registry.h"
#include "core/window.h"
#include "graphics/js_canvas_bindings.h"
#include "graphics/lua_canvas_bindings.h"

#include "device/js_device_bindings.h"
#include "audio/js_audio_bindings.h"
#include "audio/lua_audio_bindings.h"
#include "midi/js_midi_bindings.h"
#include "midi/lua_midi_bindings.h"
#include "network/js_network_bindings.h"
#include "network/lua_network_bindings.h"
#include "network/network_wrapper.h"
#include "network/js_udp_bindings.h"
#include "network/lua_udp_bindings.h"
#include "sqlite/js_sqlite_bindings.h"
#include "sqlite/lua_sqlite_bindings.h"
#include "file/js_file_bindings.h"
#include "file/lua_file_bindings.h"
#include "magneto/js_magneto_bindings.h"
#include "magneto/lua_magneto_bindings.h"
#ifdef BUDO_LLAMACPP
#include "llamacpp/js_llamacpp_bindings.h"
#include "llamacpp/lua_llamacpp_bindings.h"
#endif
#ifdef BUDO_NEURAL
#include "neural/js_neural_bindings.h"
#include "neural/lua_neural_bindings.h"
#include "neural/neural_wrapper.h"
#endif

typedef struct WasmCanvasContext WasmCanvasContext;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        
        DeviceContext *device_ctx;
        SqliteContext *sqlite_ctx;
        FileContext *file_ctx;
        MagnetoContext *magneto_ctx;
        NetworkContext *net_ctx;
        UdpContext *udp_ctx;
        RtpMidiContext *rtpmidi_ctx;
#ifdef BUDO_NEURAL
        NeuralContext *neural_ctx;
#endif

        JSRuntimeContext *js_ctx;
        JSGraphicContext *js_graphic_ctx;
        JsAudioContext *js_audio_ctx;
        JsMidiContext *js_midi_ctx;
        JsFileContext *js_file_ctx;
        JsNetworkContext *js_network_ctx;
        JsUdpContext *js_udp_ctx;
#ifdef BUDO_LLAMACPP
        JsLlamaCppContext *js_lamacpp_ctx;
#endif

        LuaCanvasContext *lua_ctx;
        LuaAudioContext *lua_audio_ctx;
        LuaMidiContext *lua_midi_ctx;
        LuaNetworkContext *lua_network_ctx;
        LuaUdpContext *lua_udp_ctx;
#ifdef BUDO_LLAMACPP
        LuaLlamaCppContext *lua_llamacpp_ctx;
#endif

        WasmCanvasContext *wasm_ctx;
    } ManagedRuntimeCommon;

    typedef enum ManagedRuntimeKind
    {
        MANAGED_RUNTIME_JAVASCRIPT,
        MANAGED_RUNTIME_LUA,
        MANAGED_RUNTIME_WEBASSEMBLY
    } ManagedRuntimeKind;

    void managed_runtime_frame(ManagedRuntimeKind kind,
                               SubsystemRegistry *subsystems,
                               JSRuntimeContext *js_context,
                               JSGraphicContext *js_graphics,
                               LuaCanvasContext *lua_context,
                               WasmCanvasContext *wasm_context,
                               SkiaCanvas *canvas,
                               int width,
                               int height,
                               Window *window,
                               InputState *input,
                               float display_density,
                               double timestamp_ms);

    void managed_runtime_shutdown(SubsystemRegistry *subsystems);

#ifdef __cplusplus
}
#endif

#endif