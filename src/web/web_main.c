#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include "core/window.h"
#include "core/input.h"
#include "core/animation_wait.h"
#include "device/device_service.h"
#include "core/app_entrypoint.h"
#include "core/app_metadata.h"
#include "core/managed_runtime.h"
#include "core/managed_subsystems.h"
#include "core/subsystem_registry.h"
#include "graphics/skia_wrapper.h"
#include "graphics/js_canvas_bindings.h"
#include "graphics/lua_canvas_bindings.h"
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
#include "web/web_input.h"
#include "web/web_sqlite.h"

extern void web_network_set_context(NetworkContext *ctx);

EM_JS(void, js_hw_lock_orientation, (const char *orientation_cstr), {
    var orientation = UTF8ToString(orientation_cstr);
    if (!orientation || orientation == "unspecified")
        return;
    if (!screen.orientation || !screen.orientation.lock)
        return;
    screen.orientation.lock(orientation).catch(function(){});
});

static const char *detect_js_entrypoint(void)
{
    AppEntrypoint entrypoint;
    return app_entrypoint_resolve("/", APP_ENTRYPOINT_JAVASCRIPT,
                                  &entrypoint, NULL, 0)
               ? (strcmp(entrypoint.filename, "main.ts") == 0 ? "/main.ts" : "/main.js")
               : NULL;
}

static bool detect_runtime(ManagedRuntimeKind *kind)
{
    AppEntrypoint entrypoint;
    char error[256];
    if (!app_entrypoint_resolve("/", APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA | APP_ENTRYPOINT_WEBASSEMBLY,
                                &entrypoint, error, sizeof(error)))
    {
        if (strstr(error, "Ambiguous"))
            fprintf(stderr, "budo-web: %s\n", error);
        else
            fprintf(stderr, "budo-web: No main.ts, main.js, main.lua, main.wat, or main.wasm found in virtual FS.\n"
                            "budo-web: Preload project files with --preload-file.\n");
        return false;
    }
    return managed_runtime_kind_from_entrypoint(entrypoint.runtime, kind);
}

typedef struct
{
    SubsystemRegistry subsystems;

    ManagedRuntimeKind runtime_kind;
    AppMetadata metadata;
    bool managed_shutdown_called;
    const char *js_entrypoint;

    SkiaPaint *wasm_paint;
    SkiaPath *wasm_paths[1024];
    int wasm_path_count;

    Window *window; 
    InputState input;

    ManagedRuntimeCommon common_contexts;

    bool graphics_failed;
    bool wasm_started;        
    bool wasm_exit_requested; 
    int wasm_exit_code;
    bool finished;
    BudoAnimationWait wasm_wait; 
} AppState;

static AppState g_state;

static ManagedFrameContext web_frame_context(AppState *state)
{
    ManagedFrameContext frame = {
        .canvas = window_get_canvas(state->window),
        .window = state->window,
        .input = &state->input,
        .display_density = window_get_dpi_scale(state->window),
    };
    window_get_size(state->window, &frame.width, &frame.height);
    return frame;
}

EM_JS(void, js_web_set_canvas_visible, (int visible), {
    var canvas = document.getElementById('canvas');
    if (!canvas) return;
    if (visible) canvas.style.removeProperty('display');
    else canvas.style.display = 'none';
});

EM_JS(void, js_web_report_exit, (int code), {
    Module.budoExitStatus = code;
    if (typeof dispatchEvent === 'function' && typeof CustomEvent === 'function')
        dispatchEvent(new CustomEvent('budoexit', {detail: {code: code}}));
});

static void web_activate_graphics(void *opaque)
{
    (void)opaque;
    if (!g_state.window && !g_state.graphics_failed)
    {
        WindowConfig window_config = {
            .title = g_state.metadata.name,
            .project_dir = "/",
            .width = 0,  
            .height = 0, 
            .resizable = true,
            .fullscreen = false,
            .vsync = true};

        js_web_set_canvas_visible(1);
        g_state.window = window_create(&window_config);
        if (!g_state.window)
        {
            fprintf(stderr, "budo-web: Failed to create window\n");
            js_web_set_canvas_visible(0);
            g_state.graphics_failed = true;
            return;
        }
    }
    if (g_state.window && g_state.runtime_kind != MANAGED_RUNTIME_WEBASSEMBLY)
    {
        ManagedFrameContext frame = web_frame_context(&g_state);
        managed_runtime_set_frame_context(g_state.runtime_kind, &g_state.common_contexts, &frame);
    }
}

static Window *web_graphics_window(void)
{
    if (!g_state.window)
        web_activate_graphics(NULL);
    return g_state.window;
}

static bool web_compose_subsystems(ManagedRuntimeKind kind)
{
    const ManagedHostConfig host = {
        .project_dir = "/",
        .sqlite_dir = "/db",
        .files_root = "/files",
        .metadata = &g_state.metadata,
        .network_created = web_network_set_context,
    };
    const char *failed_subsystem = NULL;

    if (managed_subsystems_compose(kind, &g_state.subsystems,
                                   &g_state.common_contexts, &host,
                                   &failed_subsystem))
        return true;

    fprintf(stderr, "budo-web: Failed to initialize %s subsystem\n",
            failed_subsystem ? failed_subsystem : "runtime");
    return false;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_managed_shutdown(void)
{
    if (g_state.managed_shutdown_called)
        return;

    g_state.managed_shutdown_called = true;
    managed_runtime_shutdown(&g_state.subsystems);
    g_state.common_contexts.js_ctx = NULL;
    g_state.common_contexts.lua_ctx = NULL;
    g_state.common_contexts.js_audio_ctx = NULL;
    g_state.common_contexts.lua_audio_ctx = NULL;
    g_state.common_contexts.js_midi_ctx = NULL;
    g_state.common_contexts.lua_midi_ctx = NULL;
    g_state.common_contexts.js_network_ctx = NULL;
    g_state.common_contexts.lua_network_ctx = NULL;
    g_state.common_contexts.net_ctx = NULL;
    g_state.common_contexts.sqlite_ctx = NULL;
    g_state.common_contexts.file_ctx = NULL;
    g_state.common_contexts.magneto_ctx = NULL;
    g_state.common_contexts.device_ctx = NULL;

    if (g_state.window)
    {
        window_destroy(g_state.window);
        g_state.window = NULL;
    }
}

EM_JS(void, js_web_install_managed_pagehide, (void), {
    if (Module._budoManagedPagehideInstalled)
        return;
    Module._budoManagedPagehideInstalled = true;
    var shutdownCalled = false;
    addEventListener('pagehide', function() {
        if (shutdownCalled)
            return;
        shutdownCalled = true;
        Module._budo_web_managed_shutdown();
    }, {once: true});
});

static SkiaCanvas *wasm_canvas(void)
{
    Window *window = web_graphics_window();
    return window ? window_get_canvas(window) : NULL;
}

static SkiaPaint *wasm_paint(void)
{
    if (!g_state.wasm_paint)
    {
        g_state.wasm_paint = skia_paint_create();
        if (g_state.wasm_paint)
            skia_paint_set_anti_alias(g_state.wasm_paint, true);
    }
    return g_state.wasm_paint;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_mark_ready(void)
{
    g_state.wasm_started = true;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_app_exit(int code)
{
    g_state.wasm_exit_requested = true;
    g_state.wasm_exit_code = code;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_request_graphics(void)
{
    web_graphics_window();
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_clear(int color)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_clear(canvas, (uint32_t)color);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_draw_rect(float x, float y, float w, float h)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    if (canvas && paint)
        skia_canvas_draw_rect(canvas, x, y, x + w, y + h, paint);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_draw_round_rect(float x, float y, float w, float h, float rx, float ry)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    if (canvas && paint)
        skia_canvas_draw_round_rect(canvas, x, y, x + w, y + h, rx, ry, paint);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_draw_circle(float x, float y, float radius)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    if (canvas && paint)
        skia_canvas_draw_circle(canvas, x, y, radius, paint);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_draw_line(float x1, float y1, float x2, float y2)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    if (canvas && paint)
        skia_canvas_draw_line(canvas, x1, y1, x2, y2, paint);
}

EMSCRIPTEN_KEEPALIVE
float budo_web_wasm_canvas_draw_text(const char *text, float x, float y, float font_size)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    if (canvas && paint && text)
        return skia_canvas_draw_text(canvas, text, x, y, font_size, paint);
    return 0.0f;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_set_fill_color(int color)
{
    SkiaPaint *paint = wasm_paint();
    if (paint)
    {
        skia_paint_set_color(paint, (uint32_t)color);
        skia_paint_set_style(paint, SKIA_PAINT_FILL);
    }
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_set_stroke_color(int color)
{
    SkiaPaint *paint = wasm_paint();
    if (paint)
    {
        skia_paint_set_color(paint, (uint32_t)color);
        skia_paint_set_style(paint, SKIA_PAINT_STROKE);
    }
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_set_stroke_width(float width)
{
    SkiaPaint *paint = wasm_paint();
    if (paint)
        skia_paint_set_stroke_width(paint, width);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_set_anti_alias(int enabled)
{
    SkiaPaint *paint = wasm_paint();
    if (paint)
        skia_paint_set_anti_alias(paint, enabled != 0);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_set_alpha(int alpha)
{
    SkiaPaint *paint = wasm_paint();
    if (!paint)
        return;
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    skia_paint_set_alpha(paint, (uint8_t)alpha);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_transform_save(void)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_save(canvas);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_transform_restore(void)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_restore(canvas);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_transform_translate(float x, float y)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_translate(canvas, x, y);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_transform_rotate(float degrees)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_rotate(canvas, degrees);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_transform_scale(float x, float y)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_scale(canvas, x, y);
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_path_create(void)
{
    if (g_state.wasm_path_count >= (int)(sizeof(g_state.wasm_paths) / sizeof(g_state.wasm_paths[0])))
        return -1;
    SkiaPath *path = skia_path_create();
    if (!path)
        return -1;
    g_state.wasm_paths[g_state.wasm_path_count] = path;
    return g_state.wasm_path_count++;
}

static SkiaPath *wasm_path(int id)
{
    if (id < 0 || id >= g_state.wasm_path_count)
        return NULL;
    return g_state.wasm_paths[id];
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_path_move_to(int id, float x, float y)
{
    SkiaPath *path = wasm_path(id);
    if (path)
        skia_path_move_to(path, x, y);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_path_line_to(int id, float x, float y)
{
    SkiaPath *path = wasm_path(id);
    if (path)
        skia_path_line_to(path, x, y);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_path_close(int id)
{
    SkiaPath *path = wasm_path(id);
    if (path)
        skia_path_close(path);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_draw_path(int id)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    SkiaPath *path = wasm_path(id);
    if (canvas && paint && path)
        skia_canvas_draw_path(canvas, path, paint);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_set_gradient(int kind, float a, float b, float c, float d,
                                       const uint32_t *colors, const float *stops, int count)
{
    SkiaPaint *paint = wasm_paint();
    if (!paint || !colors)
        return;
    if (kind == 0)
        skia_paint_set_linear_gradient(paint, a, b, c, d, colors, stops, count);
    else if (kind == 1)
        skia_paint_set_radial_gradient(paint, a, b, c, colors, stops, count);
    else if (kind == 2)
        skia_paint_set_sweep_gradient(paint, a, b, colors, stops, count);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_clear_gradient(void)
{
    SkiaPaint *paint = wasm_paint();
    if (paint)
        skia_paint_clear_shader(paint);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_clip_rect(float x, float y, float w, float h)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_clip_rect(canvas, x, y, x + w, y + h);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_clip_round_rect(float x, float y, float w, float h, float rx, float ry)
{
    SkiaCanvas *canvas = wasm_canvas();
    if (canvas)
        skia_canvas_clip_round_rect(canvas, x, y, x + w, y + h, rx, ry);
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_path_add_svg(int id, const char *data)
{
    SkiaPath *path = wasm_path(id);
    return path && skia_path_add_svg(path, data) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_clip_path(int id)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPath *path = wasm_path(id);
    if (canvas && path)
        skia_canvas_clip_path(canvas, path);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_canvas_save_layer(int alpha, int has_bounds, float x, float y, float w, float h,
                                     float backdrop_blur)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaRect bounds = {x, y, x + w, y + h};
    if (canvas)
        skia_canvas_save_layer(canvas, has_bounds ? &bounds : NULL,
                               (uint8_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha), backdrop_blur);
}

EMSCRIPTEN_KEEPALIVE
float budo_web_wasm_canvas_draw_paragraph(const char *text, float x, float y, float width, float font_size,
                                          int align, float line_height, int max_lines)
{
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    if (!canvas || !paint || !text)
        return 0.0f;
    return skia_canvas_draw_paragraph(canvas, text, x, y, width, font_size, line_height,
                                      align >= 0 && align <= 2 ? (SkiaTextAlign)align : SKIA_TEXT_ALIGN_LEFT,
                                      max_lines, paint, NULL)
        .height;
}

EMSCRIPTEN_KEEPALIVE
float budo_web_wasm_canvas_measure_paragraph(const char *text, float width, float font_size, float line_height,
                                             int max_lines, uint8_t *out)
{
    SkiaParagraphMetrics metrics = skia_measure_paragraph(text, width, font_size, line_height, max_lines, NULL);
    if (out)
    {
        memcpy(out, &metrics.width, 4);
        memcpy(out + 4, &metrics.height, 4);
        memcpy(out + 8, &metrics.lines, 4);
    }
    return metrics.height;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_wait_for_input(float timeout_ms)
{
    int width = 0, height = 0;
    if (g_state.window)
        window_get_size(g_state.window, &width, &height);
    budo_animation_wait_start(&g_state.wasm_wait, timeout_ms, width, height);
}

EMSCRIPTEN_KEEPALIVE
float budo_web_wasm_canvas_rich_text(const uint8_t *records, int count, int draw, float x, float y, float width,
                                     int align, float line_height, int max_lines, uint8_t *out)
{
    SkiaTextSpan spans[64];
    if (count < 0 || count > 64)
        return 0.0f;
    for (int i = 0; i < count; i++)
    {
        const char *text;
        float size;
        uint32_t color;
        memcpy(&text, records + i * 12, 4);
        memcpy(&size, records + i * 12 + 4, 4);
        memcpy(&color, records + i * 12 + 8, 4);
        spans[i] = (SkiaTextSpan){text, size, NULL, color, color != 0};
    }
    SkiaCanvas *canvas = wasm_canvas();
    SkiaPaint *paint = wasm_paint();
    SkiaParagraphMetrics metrics =
        draw ? (canvas && paint ? skia_canvas_draw_rich_text(canvas, spans, count, x, y, width, line_height,
                                                             align >= 0 && align <= 2 ? (SkiaTextAlign)align : SKIA_TEXT_ALIGN_LEFT,
                                                             max_lines, paint)
                                : (SkiaParagraphMetrics){0.0f, 0.0f, 0})
             : skia_measure_rich_text(spans, count, width, line_height, max_lines);
    if (out)
    {
        memcpy(out, &metrics.width, 4);
        memcpy(out + 4, &metrics.height, 4);
        memcpy(out + 8, &metrics.lines, 4);
    }
    return metrics.height;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_device_keep_screen_on(int enabled)
{
    return device_keep_screen_on(enabled != 0) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_device_set_clipboard_text(const char *text)
{
    return device_set_clipboard_text(text) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
char *budo_web_wasm_device_get_clipboard_text(void)
{
    return device_get_clipboard_text();
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_device_haptic(int kind)
{
    return kind >= 0 && kind < DEVICE_HAPTIC_COUNT && device_haptic((DeviceHaptic)kind) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
float budo_web_wasm_device_get_preference(int id)
{
    DevicePreferences p;
    device_get_preferences(&p);
    const float values[] = {p.dark_mode, p.reduced_motion, p.high_contrast, p.font_scale,
                            p.safe_top, p.safe_right, p.safe_bottom, p.safe_left};
    return id >= 0 && id < 8 ? values[id] : 0.0f;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_device_set_cursor(int cursor)
{
    return cursor >= 0 && cursor < DEVICE_CURSOR_COUNT && device_set_cursor((DeviceCursor)cursor) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_window_get_width(void)
{
    int width = 0, height = 0;
    if (web_graphics_window())
        window_get_size(web_graphics_window(), &width, &height);
    return width;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_window_get_height(void)
{
    int width = 0, height = 0;
    if (web_graphics_window())
        window_get_size(web_graphics_window(), &width, &height);
    return height;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_input_get_mouse_x(void)
{
    return (int)g_state.input.mouse_x;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_input_get_mouse_y(void)
{
    return (int)g_state.input.mouse_y;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_input_get_mouse_button(int button)
{
    if (button < 0 || button >= INPUT_MOUSE_MAX)
        return 0;
    return g_state.input.mouse_buttons[button] ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_gl_create_program(const char *vertex_path, const char *fragment_path)
{
    if (!web_graphics_window() || !vertex_path || !fragment_path)
        return -1;
    return window_gl_create_program(web_graphics_window(), vertex_path, fragment_path);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_draw_fullscreen(int program_id)
{
    if (web_graphics_window())
        window_gl_draw_fullscreen(web_graphics_window(), program_id);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_set_uniform_1f(int program_id, const char *name, float value)
{
    if (web_graphics_window() && name)
        window_gl_set_uniform_1f(web_graphics_window(), program_id, name, value);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_set_uniform_2f(int program_id, const char *name, float v0, float v1)
{
    if (web_graphics_window() && name)
        window_gl_set_uniform_2f(web_graphics_window(), program_id, name, v0, v1);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_set_uniform_matrix4fv(int program_id, const char *name, int values_ptr, int count)
{
    if (web_graphics_window() && name && values_ptr)
        window_gl_set_uniform_matrix4fv(web_graphics_window(), program_id, name, (const float *)(uintptr_t)values_ptr, count);
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_gl_create_buffer(void)
{
    return web_graphics_window() ? window_gl_create_buffer(web_graphics_window()) : -1;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_gl_buffer_data(int buffer_id, int target, int data_ptr, int len, int usage)
{
    if (!web_graphics_window())
        return 0;
    return window_gl_buffer_data(web_graphics_window(), buffer_id, (WindowGLBufferTarget)target,
                                 (const void *)(uintptr_t)data_ptr, (size_t)len,
                                 (WindowGLBufferUsage)usage)
               ? 1
               : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_gl_create_vertex_layout(void)
{
    return web_graphics_window() ? window_gl_create_vertex_layout(web_graphics_window()) : -1;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_set_attribute(int layout_id, int location, int buffer_id, int size, int type, int normalized, int stride, int offset, int divisor)
{
    if (web_graphics_window())
        window_gl_set_attribute(web_graphics_window(), layout_id, location, buffer_id, size,
                                (WindowGLAttrType)type, normalized != 0, stride, offset, divisor);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_set_index_buffer(int layout_id, int buffer_id, int type)
{
    if (web_graphics_window())
        window_gl_set_index_buffer(web_graphics_window(), layout_id, buffer_id, (WindowGLIndexType)type);
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_gl_draw_mesh(int program_id, int layout_id, int mode, int first, int count, int target_id, int depth_test, int depth_write, int cull, int blend, int instance_count)
{
    if (!web_graphics_window())
        return;
    WindowGLDrawState state = {
        .depth_test = depth_test != 0,
        .depth_write = depth_write != 0,
        .cull = (WindowGLCullMode)cull,
        .blend = (WindowGLBlendMode)blend,
    };
    window_gl_draw_mesh_immediate(web_graphics_window(), program_id, layout_id,
                                  (WindowGLPrimitive)mode, first, count, target_id,
                                  &state, instance_count);
}

typedef struct
{
    bool in_use;
    bool ready;
    int async_id;
    uint32_t generation;
    NetworkResponse *response;
} WebWasmNetworkSlot;

static WebWasmNetworkSlot g_web_wasm_network_slots[NETWORK_MAX_ASYNC];
static uint32_t g_web_wasm_network_generation = 1;

#define WEB_WASM_NETWORK_SLOT_BITS 5u
#define WEB_WASM_NETWORK_SLOT_MASK (NETWORK_MAX_ASYNC - 1u)
#define WEB_WASM_NETWORK_GENERATION_MASK 0x07ffffffu

static WebWasmNetworkSlot *web_wasm_network_slot(int id)
{
    if (id < 0 || id >= NETWORK_MAX_ASYNC || !g_web_wasm_network_slots[id].in_use)
        return NULL;
    return &g_web_wasm_network_slots[id];
}

static void web_wasm_network_complete(int request_id, NetworkResponse *response,
                                      const char *error, void *user_data)
{
    uint32_t token = (uint32_t)(uintptr_t)user_data;
    uint32_t generation = token >> WEB_WASM_NETWORK_SLOT_BITS;
    int slot_id = (int)(token & WEB_WASM_NETWORK_SLOT_MASK);
    WebWasmNetworkSlot *slot = web_wasm_network_slot(slot_id);
    if (!slot || slot->generation != generation ||
        slot->async_id != request_id || slot->ready)
    {
        network_response_free(response);
        return;
    }
    (void)error;
    slot->response = response;
    slot->ready = true;
}

static int web_wasm_parse_headers(char *headers, NetworkHeader *parsed)
{
    int count = 0;
    char *line = headers;
    while (line && *line && count < NETWORK_MAX_HEADERS)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        char *colon = strchr(line, ':');
        if (!colon)
            return -1;
        *colon++ = '\0';
        while (*colon == ' ' || *colon == '\t')
            colon++;
        if (!*line)
            return -1;
        parsed[count].name = line;
        parsed[count].value = colon;
        count++;
        line = next;
    }
    return count;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_network_fetch(const char *url, const char *method,
                                char *headers, const uint8_t *body, int body_len)
{
    int slot_id;
    for (slot_id = 0; slot_id < NETWORK_MAX_ASYNC; slot_id++)
        if (!g_web_wasm_network_slots[slot_id].in_use)
            break;
    if (slot_id == NETWORK_MAX_ASYNC)
        return -1;

    WebWasmNetworkSlot *slot = &g_web_wasm_network_slots[slot_id];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->async_id = -1;
    slot->generation = g_web_wasm_network_generation++ & WEB_WASM_NETWORK_GENERATION_MASK;
    if (slot->generation == 0)
        slot->generation = g_web_wasm_network_generation++ & WEB_WASM_NETWORK_GENERATION_MASK;

    NetworkHeader parsed[NETWORK_MAX_HEADERS];
    int header_count = headers && *headers ? web_wasm_parse_headers(headers, parsed) : 0;
    if (header_count < 0 || body_len < 0 || !g_state.common_contexts.net_ctx)
    {
        slot->ready = true;
        return slot_id;
    }

    slot->async_id = network_request_async(
        g_state.common_contexts.net_ctx, method && *method ? method : "GET", url,
        header_count ? parsed : NULL, header_count,
        body_len ? body : NULL, (size_t)body_len,
        web_wasm_network_complete,
        (void *)(uintptr_t)((slot->generation << WEB_WASM_NETWORK_SLOT_BITS) |
                            (uint32_t)slot_id));
    if (slot->async_id < 0)
        slot->ready = true;
    return slot_id;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_network_fetch_status(int id)
{
    WebWasmNetworkSlot *slot = web_wasm_network_slot(id);
    return !slot ? 2 : slot->ready ? (slot->response ? 1 : 2)
                                   : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_network_fetch_status_code(int id)
{
    WebWasmNetworkSlot *slot = web_wasm_network_slot(id);
    return slot && slot->ready && slot->response ? slot->response->status : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_network_fetch_response_body_len(int id)
{
    WebWasmNetworkSlot *slot = web_wasm_network_slot(id);
    return slot && slot->ready && slot->response ? (int)slot->response->body_len : 0;
}

EMSCRIPTEN_KEEPALIVE
int budo_web_wasm_network_fetch_response_body(int id, uint8_t *destination, int max_len)
{
    WebWasmNetworkSlot *slot = web_wasm_network_slot(id);
    if (!slot || !slot->ready || !slot->response || !destination || max_len <= 0)
        return 0;
    size_t count = slot->response->body_len < (size_t)max_len
                       ? slot->response->body_len
                       : (size_t)max_len;
    if (count > 0)
        memcpy(destination, slot->response->body, count);
    return (int)count;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_network_fetch_release(int id)
{
    WebWasmNetworkSlot *slot = web_wasm_network_slot(id);
    if (!slot)
        return;
    network_response_free(slot->response);
    slot->response = NULL;
    slot->in_use = false;
    slot->ready = false;
}

EMSCRIPTEN_KEEPALIVE
void budo_web_wasm_network_shutdown(void)
{
    if (g_state.common_contexts.net_ctx)
    {
        network_destroy(g_state.common_contexts.net_ctx);
        g_state.common_contexts.net_ctx = NULL;
    }
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        network_response_free(g_web_wasm_network_slots[i].response);
        memset(&g_web_wasm_network_slots[i], 0, sizeof(g_web_wasm_network_slots[i]));
    }
}

EM_JS(void, js_web_wasm_start, (void), {
    if (!Module.BudoWasmRunner) {
        let wasmMemory = null;
        const textDecoder = new TextDecoder('utf-8');

        function bytes() {
            return new Uint8Array(wasmMemory.buffer);
        }

        function f32() {
            return new Float32Array(wasmMemory.buffer);
        }

        function readString(ptr, len) {
            return textDecoder.decode(bytes().subarray(ptr, ptr + len));
        }

        function validRange(ptr, len) {
            if (ptr < 0 || len < 0) return false;
            const size = bytes().length;
            return ptr <= size && len <= size - ptr;
        }

        function cstr(value) {
            return stringToNewUTF8(value || "");
        }

        function withStrings(values, callback) {
            const ptrs = values.map(cstr);
            try {
                return callback(...ptrs);
            } finally {
                for (const ptr of ptrs) _free(ptr);
            }
        }

        function writeBytes(ptr, maxLen, source) {
            const count = Math.min(maxLen, source.length);
            bytes().set(source.subarray(0, count), ptr);
            return count;
        }

        function mat4(ptr) {
            return f32().subarray(ptr >> 2, (ptr >> 2) + 16);
        }

        function vec3(ptr) {
            return f32().subarray(ptr >> 2, (ptr >> 2) + 3);
        }

        function mat4Identity(outPtr) {
            const out = mat4(outPtr);
            out.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
        }

        function mat4Multiply(outPtr, aPtr, bPtr) {
            const out = mat4(outPtr);
            const a = Array.from(mat4(aPtr));
            const b = Array.from(mat4(bPtr));
            out.set(multiplyMat4Arrays(a, b));
        }

        function multiplyMat4Arrays(a, b) {
            const out = new Array(16);
            for (let col = 0; col < 4; col++) {
                for (let row = 0; row < 4; row++) {
                    out[col * 4 + row] =
                        a[0 * 4 + row] * b[col * 4 + 0] +
                        a[1 * 4 + row] * b[col * 4 + 1] +
                        a[2 * 4 + row] * b[col * 4 + 2] +
                        a[3 * 4 + row] * b[col * 4 + 3];
                }
            }
            return out;
        }

        function mat4Perspective(outPtr, fovy, aspect, near, far) {
            const out = mat4(outPtr);
            const f = 1.0 / Math.tan(fovy * 0.5);
            out.fill(0);
            out[0] = f / aspect;
            out[5] = f;
            out[11] = -1;
            if (far && Number.isFinite(far)) {
                out[10] = (far + near) / (near - far);
                out[14] = (2 * far * near) / (near - far);
            } else {
                out[10] = -1;
                out[14] = -2 * near;
            }
        }

        function normalize3(v) {
            const len = Math.hypot(v[0], v[1], v[2]) || 1;
            return [v[0] / len, v[1] / len, v[2] / len];
        }

        function cross(a, b) {
            return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
        }

        function dot(a, b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }

        function mat4LookAt(outPtr, eyePtr, centerPtr, upPtr) {
            const out = mat4(outPtr);
            const eye = Array.from(vec3(eyePtr));
            const center = Array.from(vec3(centerPtr));
            const up = Array.from(vec3(upPtr));
            const z = normalize3([eye[0] - center[0], eye[1] - center[1], eye[2] - center[2]]);
            const x = normalize3(cross(up, z));
            const y = cross(z, x);
            out.set([
                x[0], y[0], z[0], 0,
                x[1], y[1], z[1], 0,
                x[2], y[2], z[2], 0,
                -dot(x, eye), -dot(y, eye), -dot(z, eye), 1,
            ]);
        }

        function mat4Rotate(outPtr, aPtr, angle, axis) {
            const a = Array.from(mat4(aPtr));
            const s = Math.sin(angle);
            const c = Math.cos(angle);
            const rot = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
            if (axis === 0) {
                rot[5] = c; rot[6] = s; rot[9] = -s; rot[10] = c;
            } else if (axis === 1) {
                rot[0] = c; rot[2] = -s; rot[8] = s; rot[10] = c;
            } else {
                rot[0] = c; rot[1] = s; rot[4] = -s; rot[5] = c;
            }
            mat4(outPtr).set(multiplyMat4Arrays(a, rot));
        }

        function cwrap0(name) {
            return Module['_' + name];
        }

        function withHeapBytes(appPtr, len, callback) {
            if (len <= 0) return callback(0);
            const heapPtr = _malloc(len);
            try {
                HEAPU8.set(bytes().subarray(appPtr, appPtr + len), heapPtr);
                return callback(heapPtr);
            } finally {
                _free(heapPtr);
            }
        }

        function withHeapFloats(appPtr, count, callback) {
            return withHeapBytes(appPtr, count * 4, callback);
        }

        function gradient(kind, a, b, c, d, colorsPtr, stopsPtr, count) {
            if (count < 2 || count > 16 || !validRange(colorsPtr, count * 4) ||
                (stopsPtr >= 0 && !validRange(stopsPtr, count * 4))) return;
            withHeapBytes(colorsPtr, count * 4, (colors) => {
                if (stopsPtr < 0) return cwrap0('budo_web_wasm_canvas_set_gradient')(kind, a, b, c, d, colors, 0, count);
                return withHeapBytes(stopsPtr, count * 4, (stops) =>
                    cwrap0('budo_web_wasm_canvas_set_gradient')(kind, a, b, c, d, colors, stops, count));
            });
        }

        function richText(spansPtr, count, draw, x, y, width, align, lineHeight, maxLines, outPtr) {
            if (count < 0 || count > 64 || !validRange(spansPtr, count * 16)) return 0;
            const view = new DataView(wasmMemory.buffer);
            const texts = [];
            for (let i = 0; i < count; i++) {
                const ptr = view.getInt32(spansPtr + i * 16, true), len = view.getInt32(spansPtr + i * 16 + 4, true);
                if (!validRange(ptr, len)) return 0;
                texts.push(readString(ptr, len));
            }
            return withStrings(texts, (...pointers) => {
                const records = _malloc(Math.max(1, count * 12)), out = _malloc(12);
                try {
                    for (let i = 0; i < count; i++) {
                        HEAPU32[(records >> 2) + i * 3] = pointers[i];
                        HEAPF32[(records >> 2) + i * 3 + 1] = view.getFloat32(spansPtr + i * 16 + 8, true);
                        HEAPU32[(records >> 2) + i * 3 + 2] = view.getUint32(spansPtr + i * 16 + 12, true);
                    }
                    const height = cwrap0('budo_web_wasm_canvas_rich_text')(records, count, draw, x, y, width, align, lineHeight, maxLines, out);
                    if (outPtr >= 0 && validRange(outPtr, 12)) bytes().set(HEAPU8.subarray(out, out + 12), outPtr);
                    return height;
                } finally {
                    _free(records);
                    _free(out);
                }
            });
        }

        function clipboardText(ptr, max) {
            const text = cwrap0('budo_web_wasm_device_get_clipboard_text')();
            if (!text) return -1;
            const length = HEAPU8.indexOf(0, text) - text;
            if (max > 0 && validRange(ptr, Math.min(length, max)))
                bytes().set(HEAPU8.subarray(text, text + Math.min(length, max)), ptr);
            _free(text);
            return length;
        }

        function measureParagraph(ptr, len, width, size, lineHeight, maxLines, outPtr) {
            if (!validRange(ptr, len)) return 0;
            const out = _malloc(12);
            try {
                const height = withStrings([readString(ptr, len)], (text) =>
                    cwrap0('budo_web_wasm_canvas_measure_paragraph')(text, width, size, lineHeight, maxLines, out));
                if (outPtr >= 0 && validRange(outPtr, 12))
                    bytes().set(HEAPU8.subarray(out, out + 12), outPtr);
                return height;
            } finally {
                _free(out);
            }
        }

        const env = {
            canvas_clear: (color) => cwrap0('budo_web_wasm_canvas_clear')(color),
            canvas_draw_rect: (x, y, w, h) => cwrap0('budo_web_wasm_canvas_draw_rect')(x, y, w, h),
            canvas_draw_round_rect: (x, y, w, h, rx, ry) => cwrap0('budo_web_wasm_canvas_draw_round_rect')(x, y, w, h, rx, ry),
            canvas_draw_circle: (x, y, r) => cwrap0('budo_web_wasm_canvas_draw_circle')(x, y, r),
            canvas_draw_line: (x1, y1, x2, y2) => cwrap0('budo_web_wasm_canvas_draw_line')(x1, y1, x2, y2),
            canvas_draw_text: (ptr, len, x, y, size) => withStrings([readString(ptr, len)], (textPtr) => cwrap0('budo_web_wasm_canvas_draw_text')(textPtr, x, y, size)),
            canvas_set_fill_color: (color) => cwrap0('budo_web_wasm_canvas_set_fill_color')(color),
            canvas_set_stroke_color: (color) => cwrap0('budo_web_wasm_canvas_set_stroke_color')(color),
            canvas_set_stroke_width: (width) => cwrap0('budo_web_wasm_canvas_set_stroke_width')(width),
            canvas_set_anti_alias: (enabled) => cwrap0('budo_web_wasm_canvas_set_anti_alias')(enabled),
            canvas_set_alpha: (alpha) => cwrap0('budo_web_wasm_canvas_set_alpha')(alpha),
            transform_save: () => cwrap0('budo_web_wasm_transform_save')(),
            transform_restore: () => cwrap0('budo_web_wasm_transform_restore')(),
            transform_translate: (x, y) => cwrap0('budo_web_wasm_transform_translate')(x, y),
            transform_rotate: (degrees) => cwrap0('budo_web_wasm_transform_rotate')(degrees),
            transform_scale: (x, y) => cwrap0('budo_web_wasm_transform_scale')(x, y),
            path_create: () => cwrap0('budo_web_wasm_path_create')(),
            path_move_to: (id, x, y) => cwrap0('budo_web_wasm_path_move_to')(id, x, y),
            path_line_to: (id, x, y) => cwrap0('budo_web_wasm_path_line_to')(id, x, y),
            path_close: (id) => cwrap0('budo_web_wasm_path_close')(id),
            path_add_svg: (id, ptr, len) => len > 0 && len <= 65536 && validRange(ptr, len)
                ? withStrings([readString(ptr, len)], (data) => cwrap0('budo_web_wasm_path_add_svg')(id, data))
                : 0,
            canvas_draw_path: (id) => cwrap0('budo_web_wasm_canvas_draw_path')(id),
            canvas_set_linear_gradient: (x0, y0, x1, y1, colors, stops, count) => gradient(0, x0, y0, x1, y1, colors, stops, count),
            canvas_set_radial_gradient: (cx, cy, r, colors, stops, count) => gradient(1, cx, cy, r, 0, colors, stops, count),
            canvas_set_sweep_gradient: (cx, cy, colors, stops, count) => gradient(2, cx, cy, 0, 0, colors, stops, count),
            canvas_clear_gradient: () => cwrap0('budo_web_wasm_canvas_clear_gradient')(),
            canvas_clip_rect: (x, y, w, h) => cwrap0('budo_web_wasm_canvas_clip_rect')(x, y, w, h),
            canvas_clip_round_rect: (x, y, w, h, rx, ry) => cwrap0('budo_web_wasm_canvas_clip_round_rect')(x, y, w, h, rx, ry),
            canvas_clip_path: (id) => cwrap0('budo_web_wasm_canvas_clip_path')(id),
            canvas_save_layer: (alpha) => cwrap0('budo_web_wasm_canvas_save_layer')(alpha, 0, 0, 0, 0, 0, 0),
            canvas_save_layer_bounds: (x, y, w, h, alpha, blur) => cwrap0('budo_web_wasm_canvas_save_layer')(alpha, 1, x, y, w, h, blur),
            canvas_draw_paragraph: (ptr, len, x, y, width, size, align, lineHeight, maxLines) => validRange(ptr, len)
                ? withStrings([readString(ptr, len)], (text) => cwrap0('budo_web_wasm_canvas_draw_paragraph')(text, x, y, width, size, align, lineHeight, maxLines))
                : 0,
            canvas_measure_paragraph: measureParagraph,
            canvas_draw_rich_text: (spans, count, x, y, width, align, lineHeight, maxLines) => richText(spans, count, 1, x, y, width, align, lineHeight, maxLines, -1),
            canvas_measure_rich_text: (spans, count, width, lineHeight, maxLines, outPtr) => richText(spans, count, 0, 0, 0, width, 0, lineHeight, maxLines, outPtr),
            animation_wait_for_input: (timeout) => cwrap0('budo_web_wasm_wait_for_input')(timeout),
            device_keep_screen_on: (enabled) => cwrap0('budo_web_wasm_device_keep_screen_on')(enabled),
            device_set_clipboard_text: (ptr, len) => validRange(ptr, len) ? withStrings([readString(ptr, len)], (text) => cwrap0('budo_web_wasm_device_set_clipboard_text')(text)) : 0,
            device_get_clipboard_text: clipboardText,
            device_haptic: (kind) => cwrap0('budo_web_wasm_device_haptic')(kind),
            device_get_preference: (id) => cwrap0('budo_web_wasm_device_get_preference')(id),
            device_set_cursor: (cursor) => cwrap0('budo_web_wasm_device_set_cursor')(cursor),
            window_get_width: () => cwrap0('budo_web_wasm_window_get_width')(),
            window_get_height: () => cwrap0('budo_web_wasm_window_get_height')(),
            input_get_mouse_x: () => cwrap0('budo_web_wasm_input_get_mouse_x')(),
            input_get_mouse_y: () => cwrap0('budo_web_wasm_input_get_mouse_y')(),
            input_get_mouse_button: (button) => cwrap0('budo_web_wasm_input_get_mouse_button')(button),
            sin: Math.sin,
            cos: Math.cos,
            sqrt: Math.sqrt,
            gl_create_program: (vp, vl, fp, fl) => withStrings([readString(vp, vl), readString(fp, fl)], (v, f) => cwrap0('budo_web_wasm_gl_create_program')(v, f)),
            gl_draw_fullscreen: (program) => cwrap0('budo_web_wasm_gl_draw_fullscreen')(program),
            gl_set_uniform_1f: (program, np, nl, value) => withStrings([readString(np, nl)], (name) => cwrap0('budo_web_wasm_gl_set_uniform_1f')(program, name, value)),
            gl_set_uniform_2f: (program, np, nl, v0, v1) => withStrings([readString(np, nl)], (name) => cwrap0('budo_web_wasm_gl_set_uniform_2f')(program, name, v0, v1)),
            gl_set_uniform_matrix4fv: (program, np, nl, valuesPtr, count) => withStrings([readString(np, nl)], (name) => withHeapFloats(valuesPtr, count * 16, (heapPtr) => cwrap0('budo_web_wasm_gl_set_uniform_matrix4fv')(program, name, heapPtr, count))),
            gl_create_buffer: () => cwrap0('budo_web_wasm_gl_create_buffer')(),
            gl_buffer_data: (id, target, dataPtr, len, usage) => withHeapBytes(dataPtr, len, (heapPtr) => cwrap0('budo_web_wasm_gl_buffer_data')(id, target, heapPtr, len, usage)),
            gl_create_vertex_layout: () => cwrap0('budo_web_wasm_gl_create_vertex_layout')(),
            gl_set_attribute: (layout, loc, buffer, size, type, normalized, stride, offset, divisor) => cwrap0('budo_web_wasm_gl_set_attribute')(layout, loc, buffer, size, type, normalized, stride, offset, divisor),
            gl_set_index_buffer: (layout, buffer, type) => cwrap0('budo_web_wasm_gl_set_index_buffer')(layout, buffer, type),
            gl_draw_mesh: (program, layout, mode, first, count, target, depth, depthWrite, cull, blend, instances) => cwrap0('budo_web_wasm_gl_draw_mesh')(program, layout, mode, first, count, target, depth, depthWrite, cull, blend, instances),
            math_mat4_identity: mat4Identity,
            math_mat4_multiply: mat4Multiply,
            math_mat4_perspective: mat4Perspective,
            math_mat4_lookat: mat4LookAt,
            math_mat4_rotate_x: (out, a, angle) => mat4Rotate(out, a, angle, 0),
            math_mat4_rotate_y: (out, a, angle) => mat4Rotate(out, a, angle, 1),
            network_fetch: (up, ul, mp, ml, hp, hl, bp, bl) => {
                if (ul <= 0 || ul >= 8192 || ml < 0 || ml >= 16 || hl < 0 || hl > 64 * 1024 || bl < 0 || bl > 16 * 1024 * 1024 ||
                    !validRange(up, ul) || !validRange(mp, ml) ||
                    !validRange(hp, hl) || !validRange(bp, bl)) return -1;
                const url = readString(up, ul);
                const method = readString(mp, ml) || 'GET';
                const headers = readString(hp, hl);
                let headersFlat = "";
                try {
                    if (headers) {
                        const normalized = new Headers(JSON.parse(headers));
                        normalized.forEach((value, name) => { headersFlat += name + ': ' + value + '\n'; });
                    }
                } catch (_) {
                    return -1;
                }
                return withStrings([url, method, headersFlat], (urlPtr, methodPtr, headersPtr) =>
                    withHeapBytes(bp, bl, (bodyPtr) => cwrap0('budo_web_wasm_network_fetch')(urlPtr, methodPtr, headersPtr, bodyPtr, bl)));
            },
            network_fetch_status: (id) => cwrap0('budo_web_wasm_network_fetch_status')(id),
            network_fetch_status_code: (id) => cwrap0('budo_web_wasm_network_fetch_status_code')(id),
            network_fetch_response_body_len: (id) => cwrap0('budo_web_wasm_network_fetch_response_body_len')(id),
            network_fetch_response_body: (id, ptr, maxLen) => (maxLen <= 0 || maxLen > 16 * 1024 * 1024 || !validRange(ptr, maxLen)) ? 0 : withHeapBytes(ptr, maxLen, (heapPtr) => {
                const count = cwrap0('budo_web_wasm_network_fetch_response_body')(id, heapPtr, maxLen);
                if (count > 0) bytes().set(HEAPU8.subarray(heapPtr, heapPtr + count), ptr);
                return count;
            }),
            network_fetch_release: (id) => cwrap0('budo_web_wasm_network_fetch_release')(id),
            app_exit: (code) => {
                Module._budo_web_wasm_app_exit(code | 0);
                throw new Error(BUDO_APP_EXIT);
            },
        };

        const BUDO_APP_EXIT = 'budo:app_exit';
        const isAppExit = (error) => error && error.message === BUDO_APP_EXIT;
        Module.BudoWasmRunner = {
            start: async function() {
                try {
                    let moduleBytes = null;
                    if (FS.analyzePath('/main.wasm').exists) {
                        moduleBytes = FS.readFile('/main.wasm');
                    } else if (FS.analyzePath('/main.wat').exists) {
                        throw new Error('main.wat was found, but no compiled main.wasm is available. Rebuild the web target so the WAT file is staged as WASM.');
                    } else {
                        throw new Error('No main.wasm or main.wat found in virtual FS.');
                    }
                    const result = await WebAssembly.instantiate(moduleBytes, { env });
                    Module.BudoWasmRunner.instance = result.instance;
                    wasmMemory = result.instance.exports.memory || null;
                    if (!wasmMemory) throw new Error('WASM app does not export memory.');
                    if (typeof result.instance.exports.frame === 'function') Module._budo_web_wasm_request_graphics();
                    if (typeof result.instance.exports.init === 'function') result.instance.exports.init();
                    console.log('[budo-web] WebAssembly loaded successfully.');
                } catch (error) {
                    if (!isAppExit(error)) console.error('[budo-web] WebAssembly load failed:', error);
                }
                Module._budo_web_wasm_mark_ready();
            },
            frame: function(timestamp) {
                const instance = Module.BudoWasmRunner.instance;
                if (instance && typeof instance.exports.frame === 'function') {
                    try {
                        instance.exports.frame(timestamp);
                    } catch (error) {
                        if (!isAppExit(error)) throw error;
                    }
                }
            },
            shutdown: function() {
                Module._budo_web_wasm_network_shutdown();
                Module.BudoWasmRunner.instance = null;
            }
        };
        addEventListener('pagehide', function() { Module.BudoWasmRunner.shutdown(); }, {once: true});
    }
    Module.BudoWasmRunner.start();
});

EM_JS(void, js_web_wasm_frame, (double timestamp_ms), {
    if (Module.BudoWasmRunner) Module.BudoWasmRunner.frame(timestamp_ms);
});

EM_JS(void, js_web_wasm_shutdown, (void), {
    if (Module.BudoWasmRunner) Module.BudoWasmRunner.shutdown();
});

static bool web_exit_requested(const AppState *state, int *code)
{
    if (state->runtime_kind == MANAGED_RUNTIME_WEBASSEMBLY)
    {
        if (state->wasm_exit_requested && code)
            *code = state->wasm_exit_code;
        return state->wasm_exit_requested;
    }
    return managed_runtime_exit_requested(state->runtime_kind, &state->common_contexts, code);
}

static void web_finish(AppState *state, int code)
{
    state->finished = true;
    printf("[budo-web] Application exited with code %d\n", code);
    if (state->runtime_kind == MANAGED_RUNTIME_WEBASSEMBLY)
        js_web_wasm_shutdown();
    else
        budo_web_managed_shutdown();
    js_web_report_exit(code);
}

static bool web_tick_without_window(AppState *state, double timestamp_ms)
{
    if (state->runtime_kind == MANAGED_RUNTIME_WEBASSEMBLY)
    {
        
        if (state->common_contexts.net_ctx)
            network_async_poll(state->common_contexts.net_ctx);
        return !state->wasm_started;
    }
    managed_runtime_tick(state->runtime_kind, &state->subsystems,
                         &state->common_contexts, timestamp_ms);
    return state->window || web_exit_requested(state, NULL) ||
           managed_runtime_has_pending_work(state->runtime_kind, &state->subsystems,
                                            &state->common_contexts);
}

static EM_BOOL frame_tick(double timestamp_ms, void *user_data)
{
    AppState *state = (AppState *)user_data;
    int exit_code = 0;

    if (!state || state->finished)
        return EM_FALSE;

    if (!state->window)
    {
        bool running = web_tick_without_window(state, timestamp_ms);
        if (web_exit_requested(state, &exit_code) || state->graphics_failed || !running)
        {
            web_finish(state, state->graphics_failed ? 1 : exit_code);
            return EM_FALSE;
        }
        if (!state->window)
            return EM_TRUE;
    }

    if (!window_poll_events(state->window, &state->input))
        return EM_FALSE; 

    ManagedFrameContext frame = web_frame_context(state);
    window_begin_frame(state->window, window_get_time(state->window));

    if (state->runtime_kind == MANAGED_RUNTIME_WEBASSEMBLY)
    {
        
        if (state->common_contexts.net_ctx)
            network_async_poll(state->common_contexts.net_ctx);
        if (budo_animation_wait_due(&state->wasm_wait, &state->input, frame.width, frame.height, timestamp_ms))
            js_web_wasm_frame(timestamp_ms);
    }
    else
    {
        managed_runtime_frame(state->runtime_kind, &state->subsystems,
                              &state->common_contexts, &frame, timestamp_ms);
    }

    if (web_exit_requested(state, &exit_code))
    {
        web_finish(state, exit_code);
        return EM_FALSE;
    }

    window_present(state->window);

    input_begin_frame(&state->input);

    return EM_TRUE; 
}

static void app_start(void);

int main(void)
{
    printf("[budo-web] Budo web runtime loaded.\n");

    web_sqlite_init_fs(app_start);

    return 0;
}

static void app_start(void)
{
    
    ManagedRuntimeKind rt;
    if (!detect_runtime(&rt))
        return;

    printf("[budo-web] Detected runtime: %s\n", managed_runtime_kind_name(rt));

    AppMetadata metadata;
    app_metadata_load("/", &metadata);
    if (!metadata.valid)
    {
        fprintf(stderr, "budo-web: Invalid app.json metadata\n");
        return;
    }

    memset(&g_state, 0, sizeof(g_state));
    subsystem_registry_init(&g_state.subsystems);
    js_web_set_canvas_visible(0);
    g_state.runtime_kind = rt;
    g_state.js_entrypoint = (rt == MANAGED_RUNTIME_JAVASCRIPT) ? detect_js_entrypoint() : NULL;
    g_state.metadata = metadata;

    if (metadata.orientation[0] != '\0')
    {
        js_hw_lock_orientation(metadata.orientation);
    }

    input_init(&g_state.input);
    web_input_init(&g_state.input);

    if (rt != MANAGED_RUNTIME_WEBASSEMBLY)
    {
        const bool is_js = rt == MANAGED_RUNTIME_JAVASCRIPT;
        const char *entrypoint = is_js ? (g_state.js_entrypoint ? g_state.js_entrypoint : "/main.js")
                                       : "/main.lua";

        if (!web_compose_subsystems(rt))
        {
            budo_web_managed_shutdown();
            return;
        }

        managed_runtime_set_graphics_activation(rt, &g_state.common_contexts,
                                                web_activate_graphics, NULL);

        bool loaded = is_js ? js_runtime_load_file(g_state.common_contexts.js_ctx, entrypoint)
                            : lua_canvas_load_file(g_state.common_contexts.lua_ctx, entrypoint);
        if (!loaded)
        {
            fprintf(stderr, "budo-web: Failed to load %s\n", entrypoint);
            budo_web_managed_shutdown();
            return;
        }

        printf("[budo-web] %s loaded successfully.\n", is_js ? "JavaScript" : "Lua");
    }
    else
    {
        NetworkPolicy policy;
        network_policy_load_app_json(&policy, "/");
        g_state.common_contexts.net_ctx = network_create(&policy);
        if (!g_state.common_contexts.net_ctx)
        {
            fprintf(stderr, "budo-web: Failed to create WebAssembly network context\n");
            return;
        }
        web_network_set_context(g_state.common_contexts.net_ctx);
        js_web_wasm_start();
    }

    if (rt != MANAGED_RUNTIME_WEBASSEMBLY)
        js_web_install_managed_pagehide();

    printf("[budo-web] App: %s v%s by %s\n",
           metadata.name, metadata.version, metadata.author);

    emscripten_request_animation_frame_loop(frame_tick, &g_state);
}