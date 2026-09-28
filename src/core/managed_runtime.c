#include "managed_runtime.h"

#include "graphics/skia_wrapper.h"
#if !defined(BUDO_WEB) && !defined(BUDO_ANDROID)
#include "graphics/wasm_canvas_bindings.h"
#endif

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
                           double timestamp_ms)
{
    if (!subsystems || !canvas || !window || !input)
        return;

    if (kind == MANAGED_RUNTIME_JAVASCRIPT)
    {
        if (!js_context)
            return;
        if (js_graphics)
        {
            js_graphic_set_frame_context(js_graphics, canvas, input, window,
                                         width, height, display_density);
        }
        subsystem_registry_poll(subsystems);
        if (js_graphics)
        {
            if (js_graphic_has_animation(js_graphics))
            {
                skia_canvas_clear(canvas, SKIA_COLOR_WHITE);
                js_graphic_call_animation(js_graphics, timestamp_ms);
            }
        }
        js_runtime_process_timers(js_context, timestamp_ms);
        js_runtime_execute_pending_jobs(js_context);
        return;
    }

    if (kind == MANAGED_RUNTIME_WEBASSEMBLY)
    {
#if defined(BUDO_WEB) || defined(BUDO_ANDROID)
        (void)wasm_context;
        return;
#else
        if (!wasm_context)
            return;
        wasm_canvas_set_context(wasm_context, canvas, input, window,
                                width, height, display_density);
        wasm_canvas_midi_poll(wasm_context);
        wasm_canvas_network_poll(wasm_context);
        if (wasm_canvas_has_animation(wasm_context))
        {
            skia_canvas_clear(canvas, SKIA_COLOR_WHITE);
            wasm_canvas_call_animation(wasm_context, timestamp_ms);
        }
        return;
#endif
    }

    if (!lua_context)
        return;
    lua_canvas_set_context(lua_context, canvas, input, window,
                           width, height, display_density);
    subsystem_registry_poll(subsystems);
    if (lua_canvas_has_animation(lua_context))
    {
        skia_canvas_clear(canvas, SKIA_COLOR_WHITE);
        lua_canvas_call_animation(lua_context, timestamp_ms);
    }
}

void managed_runtime_shutdown(SubsystemRegistry *subsystems)
{
    if (subsystems)
        subsystem_registry_shutdown(subsystems);
}