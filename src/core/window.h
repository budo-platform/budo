#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "core/window_canvas_texture.h"
#include "graphics/skia_wrapper.h"
#include "core/input.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const char *title;
        const char *project_dir;
        int width;
        int height;
        bool resizable;
        bool fullscreen;
        bool vsync;
    } WindowConfig;

    typedef struct Window Window;

    Window *window_create(const WindowConfig *config);

    void window_destroy(Window *window);

    bool window_poll_events(Window *window, InputState *input);

    SkiaCanvas *window_get_canvas(Window *window);

    void window_present(Window *window);

    void window_get_size(Window *window, int *width, int *height);

    float window_get_dpi_scale(Window *window);

    bool window_should_close(Window *window);

    void window_set_title(Window *window, const char *title);

    double window_get_time(Window *window);

    double window_get_delta_time(Window *window);

    void window_resize(Window *window, int width, int height);

    void window_begin_frame(Window *window, double time_seconds);

    void window_reset_app_resources(Window *window);

    int window_gl_create_program(Window *window, const char *vertex_path, const char *fragment_path);

    int window_gl_create_program_from_source(Window *window,
                                             const char *vertex_source, size_t vertex_len,
                                             const char *fragment_source, size_t fragment_len);

    bool window_gl_destroy_program(Window *window, int program_id);

    bool window_gl_use_program(Window *window, int program_id);

    bool window_gl_bind_screen(Window *window);

    bool window_gl_bind_render_target_immediate(Window *window, int target_id);

    bool window_gl_draw_fullscreen(Window *window, int program_id);

    bool window_gl_draw_fullscreen_immediate(Window *window, int program_id, uint32_t source_texture);

    bool window_gl_draw_fullscreen_pass_immediate(Window *window, int program_id,
                                                  int source_target_id,
                                                  int destination_target_id);

    bool window_gl_draw_region_pass_immediate(Window *window, int program_id,
                                              int source_target_id,
                                              int destination_target_id,
                                              int x, int y,
                                              int width, int height);

    bool window_gl_set_uniform_1i(Window *window, int program_id, const char *name, int value);
    bool window_gl_set_uniform_1f(Window *window, int program_id, const char *name, float value);
    bool window_gl_set_uniform_2f(Window *window, int program_id, const char *name, float v0, float v1);
    bool window_gl_set_uniform_3f(Window *window, int program_id, const char *name, float v0, float v1, float v2);
    bool window_gl_set_uniform_4f(Window *window, int program_id, const char *name, float v0, float v1, float v2, float v3);

    const char *window_gl_get_error(Window *window);

    const char *window_get_project_dir(Window *window);

    int window_gl_create_render_target(Window *window, int width, int height, bool depth);

    bool window_gl_destroy_render_target(Window *window, int target_id);

    bool window_gl_resize_render_target(Window *window, int target_id, int width, int height);

    bool window_gl_draw_region(Window *window, int program_id,
                               float x, float y, float w, float h,
                               int target_id);

    bool window_gl_draw_region_immediate(Window *window, int program_id,
                                         float x, float y, float w, float h,
                                         int target_id);

    bool window_gl_bind_texture(Window *window, int program_id,
                                const char *uniform_name,
                                int render_target_id, int texture_unit);

    bool window_gl_bind_texture_immediate(Window *window, int program_id,
                                          const char *uniform_name,
                                          int render_target_id, int texture_unit);

    bool window_gl_bind_canvas_texture(Window *window, int program_id,
                                       const char *uniform_name,
                                       CanvasTexture *canvas_texture,
                                       int texture_unit);

    bool window_gl_bind_canvas_texture_immediate(Window *window, int program_id,
                                                 const char *uniform_name,
                                                 CanvasTexture *canvas_texture,
                                                 int texture_unit);

    typedef enum
    {
        WINDOW_GL_BUFFER_VERTEX = 0,
        WINDOW_GL_BUFFER_INDEX = 1
    } WindowGLBufferTarget;

    typedef enum
    {
        WINDOW_GL_USAGE_STATIC = 0,
        WINDOW_GL_USAGE_DYNAMIC = 1,
        WINDOW_GL_USAGE_STREAM = 2
    } WindowGLBufferUsage;

    typedef enum
    {
        WINDOW_GL_ATTR_FLOAT = 0,
        WINDOW_GL_ATTR_BYTE = 1,
        WINDOW_GL_ATTR_UBYTE = 2,
        WINDOW_GL_ATTR_SHORT = 3,
        WINDOW_GL_ATTR_USHORT = 4,
        WINDOW_GL_ATTR_INT = 5,
        WINDOW_GL_ATTR_UINT = 6
    } WindowGLAttrType;

    typedef enum
    {
        WINDOW_GL_INDEX_U16 = 0,
        WINDOW_GL_INDEX_U32 = 1
    } WindowGLIndexType;

    typedef enum
    {
        WINDOW_GL_PRIM_TRIANGLES = 0,
        WINDOW_GL_PRIM_TRIANGLE_STRIP = 1,
        WINDOW_GL_PRIM_TRIANGLE_FAN = 2,
        WINDOW_GL_PRIM_LINES = 3,
        WINDOW_GL_PRIM_LINE_STRIP = 4,
        WINDOW_GL_PRIM_POINTS = 5
    } WindowGLPrimitive;

    typedef enum
    {
        WINDOW_GL_CULL_NONE = 0,
        WINDOW_GL_CULL_BACK = 1,
        WINDOW_GL_CULL_FRONT = 2
    } WindowGLCullMode;

    typedef enum
    {
        WINDOW_GL_BLEND_NONE = 0,
        WINDOW_GL_BLEND_ALPHA = 1,  
        WINDOW_GL_BLEND_ADD = 2,    
        WINDOW_GL_BLEND_PREMULT = 3 
    } WindowGLBlendMode;

    typedef enum
    {
        WINDOW_GL_TEX_RGBA8 = 0,
        WINDOW_GL_TEX_RGB8 = 1,
        WINDOW_GL_TEX_R8 = 2
    } WindowGLTexFormat;

    typedef struct
    {
        bool depth_test;
        bool depth_write;
        WindowGLCullMode cull;
        WindowGLBlendMode blend;
    } WindowGLDrawState;

    int window_gl_create_buffer(Window *window);
    bool window_gl_destroy_buffer(Window *window, int buffer_id);
    bool window_gl_buffer_data(Window *window, int buffer_id,
                               WindowGLBufferTarget target,
                               const void *data, size_t size,
                               WindowGLBufferUsage usage);
    bool window_gl_buffer_sub_data(Window *window, int buffer_id,
                                   size_t offset, const void *data, size_t size);

    int window_gl_create_texture_2d(Window *window, int width, int height,
                                    WindowGLTexFormat format, const void *pixels);
    int window_gl_create_texture_2d_from_file(Window *window, const char *path);
    int window_gl_create_texture_2d_from_buffer(Window *window,
                                                const uint8_t *bytes, size_t byte_count);
    bool window_gl_get_texture_size(Window *window, int texture_id,
                                    int *out_width, int *out_height);
    int window_gl_create_texture_cube(Window *window, int size,
                                      WindowGLTexFormat format,
                                      const void *const faces[6]);
    int window_gl_create_texture_cube_from_files(Window *window,
                                                 const char *const paths[6]);
    int window_gl_create_texture_cube_from_buffers(Window *window,
                                                   const uint8_t *const bytes[6],
                                                   const size_t byte_counts[6]);
    bool window_gl_update_texture_2d(Window *window, int texture_id,
                                     int x, int y, int w, int h,
                                     const void *pixels);
    bool window_gl_destroy_texture(Window *window, int texture_id);

    int window_gl_create_vertex_layout(Window *window);
    bool window_gl_set_attribute(Window *window, int layout_id,
                                 int location, int buffer_id,
                                 int size, WindowGLAttrType type,
                                 bool normalized, int stride, int offset,
                                 int divisor);
    bool window_gl_set_index_buffer(Window *window, int layout_id,
                                    int buffer_id, WindowGLIndexType type);
    bool window_gl_destroy_vertex_layout(Window *window, int layout_id);

    bool window_gl_set_uniform_matrix3fv(Window *window, int program_id,
                                         const char *name,
                                         const float *values, int count);
    bool window_gl_set_uniform_matrix4fv(Window *window, int program_id,
                                         const char *name,
                                         const float *values, int count);
    bool window_gl_set_uniform_1iv(Window *window, int program_id, const char *name,
                                   const int *values, int count);
    bool window_gl_set_uniform_1fv(Window *window, int program_id, const char *name,
                                   const float *values, int count);
    bool window_gl_set_uniform_2fv(Window *window, int program_id, const char *name,
                                   const float *values, int count);
    bool window_gl_set_uniform_3fv(Window *window, int program_id, const char *name,
                                   const float *values, int count);
    bool window_gl_set_uniform_4fv(Window *window, int program_id, const char *name,
                                   const float *values, int count);

    bool window_gl_bind_texture_2d(Window *window, int program_id,
                                   const char *uniform_name,
                                   int texture_id, int texture_unit);
    bool window_gl_bind_texture_cube(Window *window, int program_id,
                                     const char *uniform_name,
                                     int texture_id, int texture_unit);

    bool window_gl_bind_texture_2d_immediate(Window *window, int program_id,
                                             const char *uniform_name,
                                             int texture_id, int texture_unit);
    bool window_gl_bind_texture_cube_immediate(Window *window, int program_id,
                                               const char *uniform_name,
                                               int texture_id, int texture_unit);

    bool window_gl_draw_mesh(Window *window, int program_id, int layout_id,
                             WindowGLPrimitive mode, int first, int count,
                             int target_id,
                             const WindowGLDrawState *state,
                             int instance_count);

    bool window_gl_draw_mesh_immediate(Window *window, int program_id, int layout_id,
                                       WindowGLPrimitive mode, int first, int count,
                                       int target_id,
                                       const WindowGLDrawState *state,
                                       int instance_count);

    int window_gl_get_attrib_location(Window *window, int program_id, const char *name);

#ifdef __cplusplus
}
#endif

#endif