#include "graphics/wasm_gl_bindings.h"

#include "graphics/wasm_canvas_bindings.h"
#include "core/window.h"

#include <string.h>
#include <wasm.h>

#define MAX_WASM_STRING 512
#define CANVAS_CALLBACK_CONTEXT ((WasmCanvasContext *)env)
#define WASM_GL_WINDOW wasm_canvas_current_window(CANVAS_CALLBACK_CONTEXT)

static wasm_trap_t *host_gl_create_program(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char vertex_path[MAX_WASM_STRING];
    char fragment_path[MAX_WASM_STRING];
    int program_id = -1;

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32, vertex_path, sizeof(vertex_path)) &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[2].of.i32, args[3].of.i32, fragment_path, sizeof(fragment_path)))
    {
        program_id = window_gl_create_program(WASM_GL_WINDOW, vertex_path, fragment_path);
        if (program_id < 0 && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = program_id;
    return NULL;
}

static wasm_trap_t *host_gl_create_program_from_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW && nargs >= 4)
    {
        const uint8_t *vertex_source = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32);
        const uint8_t *fragment_source = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[2].of.i32, args[3].of.i32);
        if (vertex_source && fragment_source)
        {
            results[0].of.i32 = window_gl_create_program_from_source(
                WASM_GL_WINDOW,
                (const char *)vertex_source, (size_t)args[1].of.i32,
                (const char *)fragment_source, (size_t)args[3].of.i32);
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_destroy_program(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_destroy_program(WASM_GL_WINDOW, args[0].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_use_program(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_use_program(WASM_GL_WINDOW, args[0].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_bind_screen(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_bind_screen(WASM_GL_WINDOW) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_bind_render_target_immediate(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_bind_render_target_immediate(WASM_GL_WINDOW, args[0].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_draw_fullscreen(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if ((!window_gl_bind_screen(WASM_GL_WINDOW) ||
             !window_gl_draw_fullscreen_immediate(WASM_GL_WINDOW, args[0].of.i32, 0)) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_set_uniform_1i(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_set_uniform_1i(WASM_GL_WINDOW, args[0].of.i32, name, args[3].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }

    return NULL;
}

static wasm_trap_t *host_gl_set_uniform_1f(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_set_uniform_1f(WASM_GL_WINDOW, args[0].of.i32, name, args[3].of.f32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }

    return NULL;
}

static wasm_trap_t *host_gl_set_uniform_2f(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_set_uniform_2f(WASM_GL_WINDOW, args[0].of.i32, name, args[3].of.f32, args[4].of.f32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }

    return NULL;
}

static wasm_trap_t *host_gl_set_uniform_3f(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_set_uniform_3f(WASM_GL_WINDOW, args[0].of.i32, name, args[3].of.f32, args[4].of.f32, args[5].of.f32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }

    return NULL;
}

static wasm_trap_t *host_gl_set_uniform_4f(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_set_uniform_4f(WASM_GL_WINDOW, args[0].of.i32, name, args[3].of.f32, args[4].of.f32, args[5].of.f32, args[6].of.f32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }

    return NULL;
}

static wasm_trap_t *host_gl_create_render_target(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int target_id = -1;

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        target_id = window_gl_create_render_target(
            WASM_GL_WINDOW, args[0].of.i32, args[1].of.i32,
            args[2].of.i32 != 0);
        if (target_id < 0 && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = target_id;
    return NULL;
}

static wasm_trap_t *host_gl_destroy_render_target(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_destroy_render_target(WASM_GL_WINDOW, args[0].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_resize_render_target(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int ok = 0;

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        ok = window_gl_resize_render_target(
                 WASM_GL_WINDOW, args[0].of.i32, args[1].of.i32, args[2].of.i32)
                 ? 1
                 : 0;
        if (!ok && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }

    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ok;
    return NULL;
}

static wasm_trap_t *host_gl_draw_region(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_draw_region_immediate(WASM_GL_WINDOW, args[0].of.i32,
                                             args[1].of.f32, args[2].of.f32,
                                             args[3].of.f32, args[4].of.f32,
                                             args[5].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_bind_texture(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];

    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_bind_texture_immediate(WASM_GL_WINDOW, args[0].of.i32, name,
                                              args[3].of.i32, args[4].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
        {
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }

    return NULL;
}

static wasm_trap_t *host_gl_create_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int id = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        id = window_gl_create_buffer(WASM_GL_WINDOW);
        if (id < 0 && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = id;
    return NULL;
}

static wasm_trap_t *host_gl_buffer_data(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int ok = 0;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        const void *data = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[2].of.i32, args[3].of.i32);
        if (data || args[3].of.i32 == 0)
        {
            ok = window_gl_buffer_data(WASM_GL_WINDOW,
                                       args[0].of.i32,
                                       (WindowGLBufferTarget)args[1].of.i32,
                                       data, (size_t)args[3].of.i32,
                                       (WindowGLBufferUsage)args[4].of.i32)
                     ? 1
                     : 0;
            if (!ok && window_gl_get_error(WASM_GL_WINDOW))
                wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ok;
    return NULL;
}

static wasm_trap_t *host_gl_buffer_sub_data(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int ok = 0;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        const void *data = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[2].of.i32, args[3].of.i32);
        if (data)
        {
            ok = window_gl_buffer_sub_data(WASM_GL_WINDOW,
                                           args[0].of.i32,
                                           (size_t)args[1].of.i32,
                                           data, (size_t)args[3].of.i32)
                     ? 1
                     : 0;
            if (!ok && window_gl_get_error(WASM_GL_WINDOW))
                wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ok;
    return NULL;
}

static wasm_trap_t *host_gl_destroy_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
        window_gl_destroy_buffer(WASM_GL_WINDOW, args[0].of.i32);
    return NULL;
}

static wasm_trap_t *host_gl_create_texture_2d(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int id = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        const void *pixels = NULL;
        if (args[3].of.i32 != 0 || args[4].of.i32 > 0)
            pixels = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[3].of.i32, args[4].of.i32);
        id = window_gl_create_texture_2d(WASM_GL_WINDOW,
                                         args[0].of.i32, args[1].of.i32,
                                         (WindowGLTexFormat)args[2].of.i32,
                                         pixels);
        if (id < 0 && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = id;
    return NULL;
}

static wasm_trap_t *host_gl_load_texture_2d(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int id = -1;
    char path[MAX_WASM_STRING];
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32, path, sizeof(path)))
    {
        id = window_gl_create_texture_2d_from_file(WASM_GL_WINDOW, path);
        if (id < 0 && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = id;
    return NULL;
}

static wasm_trap_t *host_gl_load_texture_2d_from_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW && nargs >= 2)
    {
        const uint8_t *data = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, args[1].of.i32);
        if (data)
            results[0].of.i32 = window_gl_create_texture_2d_from_buffer(
                WASM_GL_WINDOW, data, (size_t)args[1].of.i32);
    }
    return NULL;
}

static wasm_trap_t *host_gl_load_texture_cube(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int id = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        const void *raw = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[0].of.i32, 12 * 4);
        if (raw)
        {
            const int32_t *pl = (const int32_t *)raw;
            char paths_storage[6][MAX_WASM_STRING];
            const char *paths[6];
            int ok = 1, i;
            for (i = 0; i < 6; ++i)
            {
                if (!wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, pl[i * 2], pl[i * 2 + 1],
                                             paths_storage[i], sizeof(paths_storage[i])))
                {
                    ok = 0;
                    break;
                }
                paths[i] = paths_storage[i];
            }
            if (ok)
            {
                id = window_gl_create_texture_cube_from_files(WASM_GL_WINDOW, paths);
                if (id < 0 && window_gl_get_error(WASM_GL_WINDOW))
                    wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
            }
        }
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = id;
    return NULL;
}

static wasm_trap_t *host_gl_load_texture_cube_from_buffers(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW && nargs >= 1)
    {
        int32_t packed = args[0].of.i32;
        const uint8_t *packed_data = wasm_canvas_read_bytes(
            CANVAS_CALLBACK_CONTEXT, packed, 48);
        if (packed_data)
        {
            const uint8_t *buffers[6] = {0};
            size_t sizes[6] = {0};
            bool ok = true;
            for (int i = 0; i < 6; ++i)
            {
                int32_t ptr = 0, len = 0;
                memcpy(&ptr, packed_data + i * 8, 4);
                memcpy(&len, packed_data + i * 8 + 4, 4);
                buffers[i] = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, ptr, len);
                sizes[i] = (size_t)len;
                if (!buffers[i])
                    ok = false;
            }
            if (ok)
                results[0].of.i32 = window_gl_create_texture_cube_from_buffers(
                    WASM_GL_WINDOW, buffers, sizes);
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_update_texture_2d(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int ok = 0;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        const void *pixels = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[5].of.i32, args[6].of.i32);
        if (pixels)
        {
            ok = window_gl_update_texture_2d(WASM_GL_WINDOW,
                                             args[0].of.i32, args[1].of.i32,
                                             args[2].of.i32, args[3].of.i32,
                                             args[4].of.i32, pixels)
                     ? 1
                     : 0;
            if (!ok && window_gl_get_error(WASM_GL_WINDOW))
                wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = ok;
    return NULL;
}

static wasm_trap_t *host_gl_destroy_texture(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
        window_gl_destroy_texture(WASM_GL_WINDOW, args[0].of.i32);
    return NULL;
}

static wasm_trap_t *host_gl_create_vertex_layout(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int id = -1;
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        id = window_gl_create_vertex_layout(WASM_GL_WINDOW);
        if (id < 0 && window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = id;
    return NULL;
}

static wasm_trap_t *host_gl_destroy_vertex_layout(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
        window_gl_destroy_vertex_layout(WASM_GL_WINDOW, args[0].of.i32);
    return NULL;
}

static wasm_trap_t *host_gl_set_attribute(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_set_attribute(WASM_GL_WINDOW,
                                     args[0].of.i32, args[1].of.i32, args[2].of.i32,
                                     args[3].of.i32,
                                     (WindowGLAttrType)args[4].of.i32,
                                     args[5].of.i32 != 0,
                                     args[6].of.i32, args[7].of.i32, args[8].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    return NULL;
}

static wasm_trap_t *host_gl_set_index_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        if (!window_gl_set_index_buffer(WASM_GL_WINDOW,
                                        args[0].of.i32, args[1].of.i32,
                                        (WindowGLIndexType)args[2].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    return NULL;
}

typedef bool (*wasm_uniform_fv_fn)(Window *, int, const char *, const float *, int);

static wasm_trap_t *wasm_gl_uniform_fv_shared(
    WasmCanvasContext *ctx,
    const wasmtime_val_t *args,
    wasm_uniform_fv_fn fn, int components_per_elem)
{
    char name[MAX_WASM_STRING];
    Window *window = wasm_canvas_current_window(ctx);
    if (ctx && window &&
        wasm_canvas_read_string(ctx, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        int count = args[4].of.i32;
        size_t bytes = (size_t)count * (size_t)components_per_elem * sizeof(float);
        const void *data = wasm_canvas_read_bytes(ctx, args[3].of.i32, (int32_t)bytes);
        if (data && count > 0)
        {
            if (!fn(window, args[0].of.i32, name, (const float *)data, count) &&
                window_gl_get_error(window))
                wasm_canvas_set_error(ctx, window_gl_get_error(window));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_set_uniform_matrix3fv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    return wasm_gl_uniform_fv_shared(CANVAS_CALLBACK_CONTEXT, args, window_gl_set_uniform_matrix3fv, 9);
}

static wasm_trap_t *host_gl_set_uniform_matrix4fv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    return wasm_gl_uniform_fv_shared(CANVAS_CALLBACK_CONTEXT, args, window_gl_set_uniform_matrix4fv, 16);
}

static wasm_trap_t *host_gl_set_uniform_1fv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    return wasm_gl_uniform_fv_shared(CANVAS_CALLBACK_CONTEXT, args, window_gl_set_uniform_1fv, 1);
}

static wasm_trap_t *host_gl_set_uniform_2fv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    return wasm_gl_uniform_fv_shared(CANVAS_CALLBACK_CONTEXT, args, window_gl_set_uniform_2fv, 2);
}

static wasm_trap_t *host_gl_set_uniform_3fv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    return wasm_gl_uniform_fv_shared(CANVAS_CALLBACK_CONTEXT, args, window_gl_set_uniform_3fv, 3);
}

static wasm_trap_t *host_gl_set_uniform_4fv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    return wasm_gl_uniform_fv_shared(CANVAS_CALLBACK_CONTEXT, args, window_gl_set_uniform_4fv, 4);
}

static wasm_trap_t *host_gl_set_uniform_1iv(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        int count = args[4].of.i32;
        size_t bytes = (size_t)count * sizeof(int32_t);
        const void *data = wasm_canvas_read_bytes(CANVAS_CALLBACK_CONTEXT, args[3].of.i32, (int32_t)bytes);
        if (data && count > 0)
        {
            if (!window_gl_set_uniform_1iv(WASM_GL_WINDOW, args[0].of.i32, name,
                                           (const int *)data, count) &&
                window_gl_get_error(WASM_GL_WINDOW))
                wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
        }
    }
    return NULL;
}

static wasm_trap_t *host_gl_bind_texture_2d(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_bind_texture_2d_immediate(WASM_GL_WINDOW, args[0].of.i32, name,
                                                 args[3].of.i32, args[4].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    return NULL;
}

static wasm_trap_t *host_gl_bind_texture_cube(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    char name[MAX_WASM_STRING];
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        if (!window_gl_bind_texture_cube_immediate(WASM_GL_WINDOW, args[0].of.i32, name,
                                                   args[3].of.i32, args[4].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    return NULL;
}

static wasm_trap_t *host_gl_draw_mesh(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW)
    {
        WindowGLDrawState state;
        state.depth_test = args[6].of.i32 != 0;
        state.depth_write = args[7].of.i32 != 0;
        state.cull = (WindowGLCullMode)args[8].of.i32;
        state.blend = (WindowGLBlendMode)args[9].of.i32;
        if (!window_gl_draw_mesh_immediate(WASM_GL_WINDOW,
                                           args[0].of.i32, args[1].of.i32,
                                           (WindowGLPrimitive)args[2].of.i32,
                                           args[3].of.i32, args[4].of.i32,
                                           args[5].of.i32,
                                           &state,
                                           args[10].of.i32) &&
            window_gl_get_error(WASM_GL_WINDOW))
            wasm_canvas_set_error(CANVAS_CALLBACK_CONTEXT, window_gl_get_error(WASM_GL_WINDOW));
    }
    return NULL;
}

static wasm_trap_t *host_gl_get_attrib_location(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    int loc = -1;
    char name[MAX_WASM_STRING];
    if (CANVAS_CALLBACK_CONTEXT && WASM_GL_WINDOW &&
        wasm_canvas_read_string(CANVAS_CALLBACK_CONTEXT, args[1].of.i32, args[2].of.i32, name, sizeof(name)))
    {
        loc = window_gl_get_attrib_location(WASM_GL_WINDOW, args[0].of.i32, name);
    }
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = loc;
    return NULL;
}

static wasmtime_error_t *define_wasm_gl_function(
    wasmtime_linker_t *linker, WasmCanvasContext *context, const char *name,
    wasmtime_func_callback_t callback, const wasm_valkind_t *param_types,
    size_t param_count, const wasm_valkind_t *result_types, size_t result_count)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, param_count);
    for (size_t i = 0; i < param_count; i++)
        params.data[i] = wasm_valtype_new(param_types[i]);
    wasm_valtype_vec_new_uninitialized(&results, result_count);
    for (size_t i = 0; i < result_count; i++)
        results.data[i] = wasm_valtype_new(result_types[i]);
    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name), functype, callback, context, NULL);
    wasm_functype_delete(functype);
    return error;
}

wasmtime_error_t *wasm_gl_register(wasmtime_linker_t *linker,
                                   WasmCanvasContext *ctx)
{
    wasmtime_error_t *error = NULL;
#define define_host_function(linker_, module_, name_, callback_, params_, param_count_, results_, result_count_) \
    define_wasm_gl_function(linker_, ctx, name_, callback_, params_, param_count_, results_, result_count_)
    
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_create_program", host_gl_create_program, params, 4, results, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_create_program_from_buffer", host_gl_create_program_from_buffer, params, 4, results, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_destroy_program", host_gl_destroy_program, params, 1, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_use_program", host_gl_use_program, params, 1, NULL, 0);
        if (error)
            return error;
    }
    {
        error = define_host_function(linker, "env", "gl_bind_screen", host_gl_bind_screen, NULL, 0, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_bind_render_target", host_gl_bind_render_target_immediate, params, 1, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_draw_fullscreen", host_gl_draw_fullscreen, params, 1, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_set_uniform_1i", host_gl_set_uniform_1i, params, 4, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_F32};
        error = define_host_function(linker, "env", "gl_set_uniform_1f", host_gl_set_uniform_1f, params, 4, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_F32, WASM_F32};
        error = define_host_function(linker, "env", "gl_set_uniform_2f", host_gl_set_uniform_2f, params, 5, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(linker, "env", "gl_set_uniform_3f", host_gl_set_uniform_3f, params, 6, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
        error = define_host_function(linker, "env", "gl_set_uniform_4f", host_gl_set_uniform_4f, params, 7, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_create_render_target", host_gl_create_render_target, params, 3, results, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_destroy_render_target", host_gl_destroy_render_target, params, 1, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_resize_render_target", host_gl_resize_render_target, params, 3, results, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_I32};
        error = define_host_function(linker, "env", "gl_draw_region", host_gl_draw_region, params, 6, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_bind_texture", host_gl_bind_texture, params, 5, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_create_buffer", host_gl_create_buffer, NULL, 0, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_buffer_data", host_gl_buffer_data, params, 5, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_buffer_sub_data", host_gl_buffer_sub_data, params, 4, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_destroy_buffer", host_gl_destroy_buffer, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_create_texture_2d", host_gl_create_texture_2d, params, 5, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_load_texture_2d", host_gl_load_texture_2d, params, 2, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_load_texture_2d_from_buffer", host_gl_load_texture_2d_from_buffer, params, 2, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_load_texture_cube", host_gl_load_texture_cube, params, 1, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_load_texture_cube_from_buffer", host_gl_load_texture_cube_from_buffers, params, 1, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_update_texture_2d", host_gl_update_texture_2d, params, 7, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_destroy_texture", host_gl_destroy_texture, params, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_create_vertex_layout", host_gl_create_vertex_layout, NULL, 0, results_v, 1);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_destroy_vertex_layout", host_gl_destroy_vertex_layout, params, 1, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_set_attribute", host_gl_set_attribute, params, 9, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_set_index_buffer", host_gl_set_index_buffer, params, 3, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_set_uniform_matrix3fv", host_gl_set_uniform_matrix3fv, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_set_uniform_matrix4fv", host_gl_set_uniform_matrix4fv, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_set_uniform_1fv", host_gl_set_uniform_1fv, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_set_uniform_2fv", host_gl_set_uniform_2fv, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_set_uniform_3fv", host_gl_set_uniform_3fv, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_set_uniform_4fv", host_gl_set_uniform_4fv, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_set_uniform_1iv", host_gl_set_uniform_1iv, params, 5, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_bind_texture_2d", host_gl_bind_texture_2d, params, 5, NULL, 0);
        if (error)
            return error;
        error = define_host_function(linker, "env", "gl_bind_texture_cube", host_gl_bind_texture_cube, params, 5, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32,
                                   WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_host_function(linker, "env", "gl_draw_mesh", host_gl_draw_mesh, params, 11, NULL, 0);
        if (error)
            return error;
    }
    {
        wasm_valkind_t params[] = {WASM_I32, WASM_I32, WASM_I32};
        wasm_valkind_t results_v[] = {WASM_I32};
        error = define_host_function(linker, "env", "gl_get_attrib_location", host_gl_get_attrib_location, params, 3, results_v, 1);
        if (error)
            return error;
    }

#undef define_host_function
    return NULL;
}