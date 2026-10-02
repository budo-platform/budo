#include "managed_runtime.h"

#include "graphics/skia_wrapper.h"
#if !defined(BUDO_WEB) && !defined(BUDO_ANDROID)
#include "graphics/wasm_canvas_bindings.h"
#define BUDO_MANAGED_WASMTIME 1
#endif

bool managed_runtime_kind_from_entrypoint(AppEntrypointRuntime runtime,
                                          ManagedRuntimeKind *out)
{
    ManagedRuntimeKind kind;

    switch (runtime)
    {
    case APP_ENTRYPOINT_JAVASCRIPT:
        kind = MANAGED_RUNTIME_JAVASCRIPT;
        break;
    case APP_ENTRYPOINT_LUA:
        kind = MANAGED_RUNTIME_LUA;
        break;
    case APP_ENTRYPOINT_WEBASSEMBLY:
        kind = MANAGED_RUNTIME_WEBASSEMBLY;
        break;
    default:
        return false;
    }
    if (out)
        *out = kind;
    return true;
}

const char *managed_runtime_kind_name(ManagedRuntimeKind kind)
{
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        return "JavaScript (QuickJS)";
    case MANAGED_RUNTIME_LUA:
        return "Lua";
    case MANAGED_RUNTIME_WEBASSEMBLY:
        return "WebAssembly";
    }
    return "Unknown";
}

void managed_runtime_set_frame_context(ManagedRuntimeKind kind,
                                       ManagedRuntimeCommon *contexts,
                                       const ManagedFrameContext *frame)
{
    if (!contexts || !frame || !frame->canvas || !frame->window || !frame->input)
        return;

    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        if (contexts->js_graphic_ctx)
            js_graphic_set_frame_context(contexts->js_graphic_ctx, frame->canvas,
                                         frame->input, frame->window,
                                         frame->width, frame->height,
                                         frame->display_density);
        break;
    case MANAGED_RUNTIME_LUA:
        if (contexts->lua_ctx)
            lua_canvas_set_context(contexts->lua_ctx, frame->canvas,
                                   frame->input, frame->window,
                                   frame->width, frame->height,
                                   frame->display_density);
        break;
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        if (contexts->wasm_ctx)
            wasm_canvas_set_context(contexts->wasm_ctx, frame->canvas,
                                    frame->input, frame->window,
                                    frame->width, frame->height,
                                    frame->display_density);
#endif
        break;
    }
}

static bool runtime_has_animation(ManagedRuntimeKind kind,
                                  const ManagedRuntimeCommon *contexts)
{
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        return contexts->js_graphic_ctx &&
               js_graphic_has_animation(contexts->js_graphic_ctx);
    case MANAGED_RUNTIME_LUA:
        return lua_canvas_has_animation(contexts->lua_ctx);
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        return wasm_canvas_has_animation(contexts->wasm_ctx);
#else
        return false;
#endif
    }
    return false;
}

static void runtime_call_animation(ManagedRuntimeKind kind,
                                   ManagedRuntimeCommon *contexts,
                                   double timestamp_ms)
{
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        js_graphic_call_animation(contexts->js_graphic_ctx, timestamp_ms);
        break;
    case MANAGED_RUNTIME_LUA:
        lua_canvas_call_animation(contexts->lua_ctx, timestamp_ms);
        break;
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        wasm_canvas_call_animation(contexts->wasm_ctx, timestamp_ms);
#endif
        break;
    }
}

static bool runtime_is_loaded(ManagedRuntimeKind kind,
                              const ManagedRuntimeCommon *contexts)
{
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        return contexts->js_ctx != NULL;
    case MANAGED_RUNTIME_LUA:
        return contexts->lua_ctx != NULL;
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        return contexts->wasm_ctx != NULL;
#else
        return false;
#endif
    }
    return false;
}

void managed_runtime_frame(ManagedRuntimeKind kind,
                           SubsystemRegistry *subsystems,
                           ManagedRuntimeCommon *contexts,
                           const ManagedFrameContext *frame,
                           double timestamp_ms)
{
    if (!subsystems || !contexts || !frame || !frame->canvas ||
        !frame->window || !frame->input || !runtime_is_loaded(kind, contexts))
        return;

    managed_runtime_set_frame_context(kind, contexts, frame);
    subsystem_registry_poll(subsystems);
    if (runtime_has_animation(kind, contexts))
    {
        skia_canvas_clear(frame->canvas, SKIA_COLOR_WHITE);
        runtime_call_animation(kind, contexts, timestamp_ms);
    }
    if (kind == MANAGED_RUNTIME_JAVASCRIPT)
    {
        js_runtime_process_timers(contexts->js_ctx, timestamp_ms);
        js_runtime_execute_pending_jobs(contexts->js_ctx);
    }
}

void managed_runtime_set_graphics_activation(ManagedRuntimeKind kind,
                                             ManagedRuntimeCommon *contexts,
                                             BudoGraphicsActivateFn activate,
                                             void *opaque)
{
    if (!contexts)
        return;
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        if (contexts->js_graphic_ctx)
            budo_graphics_activation_set(&contexts->js_graphic_ctx->activation, activate, opaque);
        break;
    case MANAGED_RUNTIME_LUA:
        if (contexts->lua_ctx)
            budo_graphics_activation_set(&contexts->lua_ctx->activation, activate, opaque);
        break;
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        wasm_canvas_set_graphics_activation(contexts->wasm_ctx, activate, opaque);
#endif
        break;
    }
}

bool managed_runtime_graphics_requested(ManagedRuntimeKind kind,
                                        const ManagedRuntimeCommon *contexts)
{
    if (!contexts)
        return false;
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        return contexts->js_graphic_ctx && contexts->js_graphic_ctx->activation.requested;
    case MANAGED_RUNTIME_LUA:
        return contexts->lua_ctx && contexts->lua_ctx->activation.requested;
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        return wasm_canvas_graphics_requested(contexts->wasm_ctx);
#else
        return false;
#endif
    }
    return false;
}

bool managed_runtime_exit_requested(ManagedRuntimeKind kind,
                                    const ManagedRuntimeCommon *contexts, int *code)
{
    if (!contexts)
        return false;
    switch (kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        return js_runtime_exit_requested(contexts->js_ctx, code);
    case MANAGED_RUNTIME_LUA:
        return lua_canvas_exit_requested(contexts->lua_ctx, code);
    case MANAGED_RUNTIME_WEBASSEMBLY:
#ifdef BUDO_MANAGED_WASMTIME
        return wasm_canvas_exit_requested(contexts->wasm_ctx, code);
#else
        return false;
#endif
    }
    return false;
}

bool managed_runtime_has_pending_work(ManagedRuntimeKind kind,
                                      const SubsystemRegistry *subsystems,
                                      ManagedRuntimeCommon *contexts)
{
    if (subsystem_registry_has_pending_work(subsystems))
        return true;
    return kind == MANAGED_RUNTIME_JAVASCRIPT && contexts &&
           js_runtime_has_pending_work(contexts->js_ctx);
}

void managed_runtime_tick(ManagedRuntimeKind kind, SubsystemRegistry *subsystems,
                          ManagedRuntimeCommon *contexts, double timestamp_ms)
{
    if (!subsystems || !contexts || !runtime_is_loaded(kind, contexts))
        return;
    subsystem_registry_poll(subsystems);
    if (kind == MANAGED_RUNTIME_JAVASCRIPT)
    {
        js_runtime_process_timers(contexts->js_ctx, timestamp_ms);
        js_runtime_execute_pending_jobs(contexts->js_ctx);
    }
}

void managed_runtime_shutdown(SubsystemRegistry *subsystems)
{
    if (subsystems)
        subsystem_registry_shutdown(subsystems);
}