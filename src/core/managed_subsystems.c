#include "managed_subsystems.h"

#include "core/managed_basics.h"
#include "core/managed_subsystem_adapters.h"
#include "core/subsystem_composition.h"
#include "file/file_wrapper.h"

#ifndef BUDO_WEB
#define BUDO_MANAGED_UDP 1
#include "midi/rtpmidi.h"
#include "network/js_udp_bindings.h"
#include "network/lua_udp_bindings.h"
#endif

#ifdef BUDO_NEURAL
#include "neural/js_neural_bindings.h"
#include "neural/lua_neural_bindings.h"
#include "neural/neural_wrapper.h"
#endif

typedef struct ManagedSubsystemHost
{
    ManagedRuntimeCommon *contexts;
    const ManagedHostConfig *config;
} ManagedSubsystemHost;

typedef struct ManagedSubsystemEntry
{
    SubsystemDescriptor descriptor;
    ManagedSubsystemFeature feature;
} ManagedSubsystemEntry;

#define MANAGED_SUBSYSTEM(name_, role_, feature_, initialize_, poll_, cleanup_, optional_) \
    MANAGED_SUBSYSTEM_BUSY(name_, role_, feature_, initialize_, poll_, cleanup_, NULL, optional_)

#define MANAGED_SUBSYSTEM_BUSY(name_, role_, feature_, initialize_, poll_, cleanup_, pending_, optional_) \
    {{name_, role_, initialize_, {.poll = poll_, .cleanup = cleanup_, .has_pending_work = pending_}, optional_}, feature_}

static const char *host_sqlite_dir(const ManagedHostConfig *config)
{
    return config->sqlite_dir ? config->sqlite_dir : config->project_dir;
}

static const char *host_files_root(const ManagedHostConfig *config)
{
    return config->files_root ? config->files_root : config->project_dir;
}

static void host_publish_file(ManagedSubsystemHost *host, FileContext *file_ctx)
{
    host->contexts->file_ctx = file_ctx;
    if (file_ctx)
        file_set_write_root(file_ctx, host_files_root(host->config));
}

static void host_publish_network(ManagedSubsystemHost *host, NetworkContext *network)
{
    host->contexts->net_ctx = network;
    if (network && host->config->network_created)
        host->config->network_created(network);
}

#ifdef BUDO_NEURAL
static bool host_neural_enabled(const ManagedSubsystemHost *host)
{
    return host->config->metadata && host->config->metadata->neural_enabled;
}

static void neural_cleanup_adapter(void *opaque)
{
    neural_destroy((NeuralContext *)opaque);
}
#endif

#ifdef BUDO_MANAGED_UDP
static void js_udp_poll_adapter(void *opaque)
{
    js_udp_poll((JsUdpContext *)opaque);
}

static void js_udp_cleanup_adapter(void *opaque)
{
    js_udp_cleanup((JsUdpContext *)opaque);
}

static void lua_udp_poll_adapter(void *opaque)
{
    lua_udp_poll((LuaUdpContext *)opaque);
}

static bool js_udp_pending_adapter(void *opaque)
{
    return js_udp_has_pending_work((JsUdpContext *)opaque);
}

static bool lua_udp_pending_adapter(void *opaque)
{
    return lua_udp_has_pending_work((LuaUdpContext *)opaque);
}

static void lua_udp_cleanup_adapter(void *opaque)
{
    lua_udp_cleanup((LuaUdpContext *)opaque);
}

static void rtpmidi_cleanup_adapter(void *opaque)
{
    rtpmidi_destroy((RtpMidiContext *)opaque);
}

static void *init_rtpmidi(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->rtpmidi_ctx = c->udp_ctx ? rtpmidi_create(c->udp_ctx) : NULL;
    return c->rtpmidi_ctx;
}
#endif

static void *init_js_core(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    host->contexts->js_ctx = js_runtime_create(host->config->project_dir);
    return host->contexts->js_ctx;
}

static void *init_js_canvas(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->js_graphic_ctx = js_graphic_init(c->js_ctx);
    return c->js_graphic_ctx;
}

static void *init_js_audio(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->js_audio_ctx = js_audio_init(c->js_ctx->context, host->config->project_dir);
    return c->js_audio_ctx;
}

static void *init_js_sqlite(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->sqlite_ctx = js_sqlite_init(c->js_ctx->context, host_sqlite_dir(host->config));
    return c->sqlite_ctx;
}

static void *init_js_file(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->js_file_ctx = js_file_init(c->js_ctx->context, host->config->project_dir);
    host_publish_file(host, js_file_context(c->js_file_ctx));
    return c->js_file_ctx;
}

#ifdef BUDO_LLAMACPP
static void *init_js_llamacpp(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->js_llamacpp_ctx = js_llamacpp_init(c->js_ctx->context, c->file_ctx);
    return c->js_llamacpp_ctx;
}
#endif

static void *init_js_network(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->js_network_ctx = js_network_init(c->js_ctx->context, host->config->project_dir);
    host_publish_network(host, js_network_context(c->js_network_ctx));
    return c->js_network_ctx;
}

static void *init_js_magneto(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->magneto_ctx = js_magneto_init(c->js_ctx->context);
    return c->magneto_ctx;
}

static void *init_js_device(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->device_ctx = managed_basics_init_js(c->js_ctx->context);
    return c->device_ctx;
}

#ifdef BUDO_MANAGED_UDP
static void *init_js_udp(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->js_udp_ctx = js_udp_init(c->js_ctx->context);
    c->udp_ctx = js_udp_context(c->js_udp_ctx);
    return c->js_udp_ctx;
}
#endif

static void *init_js_midi(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->js_midi_ctx = js_midi_init(c->js_ctx->context);
#ifdef BUDO_MANAGED_UDP
    if (c->js_midi_ctx)
        js_midi_set_rtpmidi(c->js_midi_ctx, c->rtpmidi_ctx);
#endif
    return c->js_midi_ctx;
}

#ifdef BUDO_NEURAL
static void *init_js_neural(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    if (!host_neural_enabled(host))
        return NULL;
    c->neural_ctx = neural_create(host->config->project_dir);
    if (c->neural_ctx)
        js_neural_init(c->js_ctx->context, c->neural_ctx);
    return c->neural_ctx;
}
#endif

static const ManagedSubsystemEntry js_subsystems[] = {
    MANAGED_SUBSYSTEM("JavaScript core", SUBSYSTEM_ROLE_CORE, MANAGED_FEATURE_NONE, init_js_core, NULL, managed_js_core_cleanup, false),
    MANAGED_SUBSYSTEM("JavaScript canvas", SUBSYSTEM_ROLE_CANVAS, MANAGED_FEATURE_NONE, init_js_canvas, NULL, managed_js_canvas_cleanup, false),
    MANAGED_SUBSYSTEM("JavaScript audio", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_AUDIO, init_js_audio, NULL, managed_js_audio_cleanup, true),
    MANAGED_SUBSYSTEM("JavaScript SQLite", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_js_sqlite, NULL, managed_js_sqlite_cleanup, true),
    MANAGED_SUBSYSTEM_BUSY("JavaScript file", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_js_file, managed_js_file_poll, managed_js_file_cleanup, managed_js_file_pending, true),
#ifdef BUDO_LLAMACPP
    MANAGED_SUBSYSTEM_BUSY("JavaScript llama.cpp", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_js_llamacpp, managed_js_llamacpp_poll, managed_js_llamacpp_cleanup, managed_js_llamacpp_pending, true),
#endif
    MANAGED_SUBSYSTEM_BUSY("JavaScript network", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_js_network, managed_js_network_poll, managed_js_network_cleanup, managed_js_network_pending, true),
    MANAGED_SUBSYSTEM("JavaScript magnetometer", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_MAGNETO, init_js_magneto, NULL, managed_js_magneto_cleanup, true),
    MANAGED_SUBSYSTEM("JavaScript device", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_js_device, NULL, managed_device_cleanup, false),
#ifdef BUDO_MANAGED_UDP
    MANAGED_SUBSYSTEM_BUSY("JavaScript UDP", SUBSYSTEM_ROLE_UDP, MANAGED_FEATURE_NONE, init_js_udp, js_udp_poll_adapter, js_udp_cleanup_adapter, js_udp_pending_adapter, true),
    MANAGED_SUBSYSTEM("RTP-MIDI", SUBSYSTEM_ROLE_RTP_MIDI, MANAGED_FEATURE_NONE, init_rtpmidi, NULL, rtpmidi_cleanup_adapter, true),
#endif
    MANAGED_SUBSYSTEM_BUSY("JavaScript MIDI", SUBSYSTEM_ROLE_MIDI, MANAGED_FEATURE_MIDI, init_js_midi, managed_js_midi_poll, managed_js_midi_cleanup, managed_js_midi_pending, false),
#ifdef BUDO_NEURAL
    MANAGED_SUBSYSTEM("JavaScript neural", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_js_neural, NULL, neural_cleanup_adapter, true),
#endif
};

static void *init_lua_canvas(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    host->contexts->lua_ctx = lua_canvas_create(host->config->project_dir);
    return host->contexts->lua_ctx;
}

static void *init_lua_audio(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->lua_audio_ctx = lua_audio_init(c->lua_ctx->L, host->config->project_dir);
    return c->lua_audio_ctx;
}

static void *init_lua_sqlite(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->sqlite_ctx = lua_sqlite_init(c->lua_ctx->L, host_sqlite_dir(host->config));
    return c->sqlite_ctx;
}

static void *init_lua_file(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    host_publish_file(host, lua_file_init(host->contexts->lua_ctx->L,
                                          host->config->project_dir));
    return host->contexts->file_ctx;
}

#ifdef BUDO_LLAMACPP
static void *init_lua_llamacpp(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->lua_llamacpp_ctx = lua_llamacpp_init(c->lua_ctx->L, c->file_ctx);
    return c->lua_llamacpp_ctx;
}
#endif

static void *init_lua_network(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    c->lua_network_ctx = lua_network_init(c->lua_ctx->L, host->config->project_dir);
    host_publish_network(host, lua_network_context(c->lua_network_ctx));
    return c->lua_network_ctx;
}

static void *init_lua_magneto(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->magneto_ctx = lua_magneto_init(c->lua_ctx->L);
    return c->magneto_ctx;
}

static void *init_lua_device(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->device_ctx = managed_basics_init_lua(c->lua_ctx->L);
    return c->device_ctx;
}

#ifdef BUDO_MANAGED_UDP
static void *init_lua_udp(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->lua_udp_ctx = lua_udp_init(c->lua_ctx->L);
    c->udp_ctx = lua_udp_context(c->lua_udp_ctx);
    return c->lua_udp_ctx;
}
#endif

static void *init_lua_midi(void *opaque)
{
    ManagedRuntimeCommon *c = ((ManagedSubsystemHost *)opaque)->contexts;
    c->lua_midi_ctx = lua_midi_init(c->lua_ctx->L);
#ifdef BUDO_MANAGED_UDP
    if (c->lua_midi_ctx)
        lua_midi_set_rtpmidi(c->lua_midi_ctx, c->rtpmidi_ctx);
#endif
    return c->lua_midi_ctx;
}

#ifdef BUDO_NEURAL
static void *init_lua_neural(void *opaque)
{
    ManagedSubsystemHost *host = opaque;
    ManagedRuntimeCommon *c = host->contexts;
    if (!host_neural_enabled(host))
        return NULL;
    c->neural_ctx = neural_create(host->config->project_dir);
    if (c->neural_ctx)
        lua_neural_init(c->lua_ctx->L, c->neural_ctx);
    return c->neural_ctx;
}
#endif

static const ManagedSubsystemEntry lua_subsystems[] = {
    MANAGED_SUBSYSTEM("Lua canvas", SUBSYSTEM_ROLE_CANVAS, MANAGED_FEATURE_NONE, init_lua_canvas, NULL, managed_lua_canvas_cleanup, false),
    MANAGED_SUBSYSTEM("Lua audio", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_AUDIO, init_lua_audio, NULL, managed_lua_audio_cleanup, true),
    MANAGED_SUBSYSTEM("Lua SQLite", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_lua_sqlite, NULL, managed_lua_sqlite_cleanup, true),
    MANAGED_SUBSYSTEM("Lua file", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_lua_file, NULL, managed_lua_file_cleanup, true),
#ifdef BUDO_LLAMACPP
    MANAGED_SUBSYSTEM_BUSY("Lua llama.cpp", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_lua_llamacpp, managed_lua_llamacpp_poll, managed_lua_llamacpp_cleanup, managed_lua_llamacpp_pending, true),
#endif
    MANAGED_SUBSYSTEM_BUSY("Lua network", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_lua_network, managed_lua_network_poll, managed_lua_network_cleanup, managed_lua_network_pending, true),
    MANAGED_SUBSYSTEM("Lua magnetometer", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_MAGNETO, init_lua_magneto, NULL, managed_lua_magneto_cleanup, true),
    MANAGED_SUBSYSTEM("Lua device", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_lua_device, NULL, managed_device_cleanup, false),
#ifdef BUDO_MANAGED_UDP
    MANAGED_SUBSYSTEM_BUSY("Lua UDP", SUBSYSTEM_ROLE_UDP, MANAGED_FEATURE_NONE, init_lua_udp, lua_udp_poll_adapter, lua_udp_cleanup_adapter, lua_udp_pending_adapter, true),
    MANAGED_SUBSYSTEM("RTP-MIDI", SUBSYSTEM_ROLE_RTP_MIDI, MANAGED_FEATURE_NONE, init_rtpmidi, NULL, rtpmidi_cleanup_adapter, true),
#endif
    MANAGED_SUBSYSTEM_BUSY("Lua MIDI", SUBSYSTEM_ROLE_MIDI, MANAGED_FEATURE_MIDI, init_lua_midi, managed_lua_midi_poll, managed_lua_midi_cleanup, managed_lua_midi_pending, false),
#ifdef BUDO_NEURAL
    MANAGED_SUBSYSTEM("Lua neural", SUBSYSTEM_ROLE_SERVICE, MANAGED_FEATURE_NONE, init_lua_neural, NULL, neural_cleanup_adapter, true),
#endif
};

#undef MANAGED_SUBSYSTEM_BUSY
#undef MANAGED_SUBSYSTEM

bool managed_subsystems_compose(ManagedRuntimeKind kind,
                                SubsystemRegistry *registry,
                                ManagedRuntimeCommon *contexts,
                                const ManagedHostConfig *config,
                                const char **failed_subsystem)
{
    const ManagedSubsystemEntry *entries;
    size_t entry_count;
    SubsystemDescriptor descriptors[SUBSYSTEM_REGISTRY_CAPACITY];
    size_t descriptor_count = 0;
    ManagedSubsystemHost host;

    if (failed_subsystem)
        *failed_subsystem = NULL;
    if (!registry || !contexts || !config || !config->project_dir)
        return false;

    if (kind == MANAGED_RUNTIME_JAVASCRIPT)
    {
        entries = js_subsystems;
        entry_count = sizeof(js_subsystems) / sizeof(js_subsystems[0]);
    }
    else if (kind == MANAGED_RUNTIME_LUA)
    {
        entries = lua_subsystems;
        entry_count = sizeof(lua_subsystems) / sizeof(lua_subsystems[0]);
    }
    else
    {
        return false;
    }

    for (size_t i = 0; i < entry_count; i++)
    {
        if (entries[i].feature != MANAGED_FEATURE_NONE &&
            config->feature_available &&
            !config->feature_available(entries[i].feature))
            continue;
        if (descriptor_count >= SUBSYSTEM_REGISTRY_CAPACITY)
        {
            if (failed_subsystem)
                *failed_subsystem = entries[i].descriptor.name;
            return false;
        }
        descriptors[descriptor_count++] = entries[i].descriptor;
    }

    host.contexts = contexts;
    host.config = config;
    return subsystem_compose(registry, &host, descriptors, descriptor_count,
                             failed_subsystem);
}