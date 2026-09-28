#include <budo/budo.h>
#include "core/window.h"
#include "native/native_host_internal.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct Window
{
    int next_id;
    int draw_count;
    int pass_count;
    int region_pass_count;
    int destroyed_targets;
    int resized_target;
    int pass_source;
    int pass_destination;
    int region_x;
    int region_y;
    int region_width;
    int region_height;
    int destroyed_textures;
    size_t last_size;
    char vertex_path[64];
    char fragment_path[64];
    char error[128];
};

const char *window_gl_get_error(Window *window) { return window->error; }
int window_gl_create_program_from_source(Window *window, const char *vs,
                                         size_t vl, const char *fs, size_t fl)
{
    (void)fs;
    (void)fl;
    if (vl >= 4 && memcmp(vs, "fail", 4) == 0)
    {
        strcpy(window->error, "synthetic shader failure");
        return -1;
    }
    return ++window->next_id;
}
int window_gl_create_program(Window *window, const char *vertex_path,
                             const char *fragment_path)
{
    if (strcmp(vertex_path, "missing.vert") == 0)
    {
        strcpy(window->error, "synthetic shader file failure");
        return -1;
    }
    snprintf(window->vertex_path, sizeof(window->vertex_path), "%s", vertex_path);
    snprintf(window->fragment_path, sizeof(window->fragment_path), "%s", fragment_path);
    return ++window->next_id;
}
bool window_gl_destroy_program(Window *w, int id)
{
    (void)w;
    return id > 0;
}
int window_gl_get_attrib_location(Window *w, int id, const char *name)
{
    (void)w;
    (void)id;
    return strcmp(name, "position") == 0 ? 3 : -1;
}
#define UNIFORM_STUB(name, args) \
    bool name args               \
    {                            \
        (void)window;            \
        (void)program_id;        \
        (void)uniform_name;      \
        return true;             \
    }
UNIFORM_STUB(window_gl_set_uniform_1i, (Window * window, int program_id, const char *uniform_name, int value))
UNIFORM_STUB(window_gl_set_uniform_1f, (Window * window, int program_id, const char *uniform_name, float value))
UNIFORM_STUB(window_gl_set_uniform_2f, (Window * window, int program_id, const char *uniform_name, float x, float y))
UNIFORM_STUB(window_gl_set_uniform_3f, (Window * window, int program_id, const char *uniform_name, float x, float y, float z))
UNIFORM_STUB(window_gl_set_uniform_4f, (Window * window, int program_id, const char *uniform_name, float x, float y, float z, float v))
UNIFORM_STUB(window_gl_set_uniform_matrix4fv, (Window * window, int program_id, const char *uniform_name, const float *values, int count))
#undef UNIFORM_STUB
int window_gl_create_buffer(Window *window) { return ++window->next_id; }
bool window_gl_destroy_buffer(Window *w, int id)
{
    (void)w;
    return id > 0;
}
bool window_gl_buffer_data(Window *window, int id, WindowGLBufferTarget target,
                           const void *data, size_t size, WindowGLBufferUsage usage)
{
    (void)id;
    (void)target;
    (void)data;
    (void)usage;
    window->last_size = size;
    return true;
}
bool window_gl_buffer_sub_data(Window *w, int id, size_t offset,
                               const void *data, size_t size)
{
    (void)w;
    (void)id;
    (void)offset;
    (void)data;
    (void)size;
    return true;
}
int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    (void)width;
    (void)height;
    (void)format;
    (void)pixels;
    return ++window->next_id;
}
int window_gl_create_texture_2d_from_file(Window *window, const char *path)
{
    if (strcmp(path, "missing.png") == 0)
    {
        strcpy(window->error, "synthetic image file failure");
        return -1;
    }
    return ++window->next_id;
}
int window_gl_create_texture_2d_from_buffer(Window *window,
                                            const uint8_t *bytes,
                                            size_t byte_count)
{
    if (!bytes || byte_count == 0 || bytes[0] == 0xff)
    {
        strcpy(window->error, "synthetic image decode failure");
        return -1;
    }
    return ++window->next_id;
}
bool window_gl_get_texture_size(Window *window, int id,
                                int *out_width, int *out_height)
{
    (void)window;
    if (id <= 0 || !out_width || !out_height)
        return false;
    *out_width = 4;
    *out_height = 3;
    return true;
}
bool window_gl_update_texture_2d(Window *w, int id, int x, int y, int width,
                                 int height, const void *pixels)
{
    (void)w;
    (void)id;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)pixels;
    return true;
}
bool window_gl_destroy_texture(Window *w, int id)
{
    w->destroyed_textures++;
    return id > 0;
}
bool window_gl_bind_texture_2d_immediate(Window *w, int program_id,
                                         const char *name, int texture_id,
                                         int unit)
{
    (void)w;
    (void)program_id;
    (void)name;
    (void)texture_id;
    return unit >= 0;
}
int window_gl_create_vertex_layout(Window *window) { return ++window->next_id; }
bool window_gl_set_attribute(Window *w, int layout_id, int location,
                             int buffer_id, int size, WindowGLAttrType type,
                             bool normalized, int stride, int offset, int divisor)
{
    (void)w;
    (void)layout_id;
    (void)location;
    (void)buffer_id;
    (void)size;
    (void)type;
    (void)normalized;
    (void)stride;
    (void)offset;
    (void)divisor;
    return true;
}
bool window_gl_set_index_buffer(Window *w, int layout_id, int buffer_id,
                                WindowGLIndexType type)
{
    (void)w;
    (void)layout_id;
    (void)buffer_id;
    (void)type;
    return true;
}
bool window_gl_destroy_vertex_layout(Window *w, int id)
{
    (void)w;
    return id > 0;
}
bool window_gl_draw_mesh_immediate(Window *window, int program_id, int layout_id,
                                   WindowGLPrimitive mode, int first, int count,
                                   int target_id, const WindowGLDrawState *state,
                                   int instance_count)
{
    (void)program_id;
    (void)layout_id;
    (void)mode;
    (void)first;
    (void)count;
    (void)target_id;
    (void)state;
    (void)instance_count;
    window->draw_count++;
    return true;
}
int window_gl_create_render_target(Window *window, int width, int height,
                                   bool depth)
{
    (void)depth;
    if (width == 13)
    {
        strcpy(window->error, "synthetic render target failure");
        return -1;
    }
    return ++window->next_id;
}
bool window_gl_resize_render_target(Window *window, int id, int width, int height)
{
    (void)height;
    if (width == 13)
    {
        strcpy(window->error, "synthetic resize failure");
        return false;
    }
    window->resized_target = id;
    return true;
}
bool window_gl_destroy_render_target(Window *window, int id)
{
    window->destroyed_targets++;
    return id > 0;
}
bool window_gl_draw_fullscreen_pass_immediate(Window *window, int program_id,
                                              int source_target_id,
                                              int destination_target_id)
{
    (void)program_id;
    window->pass_count++;
    window->pass_source = source_target_id;
    window->pass_destination = destination_target_id;
    return true;
}
bool window_gl_draw_region_pass_immediate(Window *window, int program_id,
                                          int source_target_id,
                                          int destination_target_id,
                                          int x, int y,
                                          int width, int height)
{
    (void)program_id;
    if (x == 77)
    {
        strcpy(window->error, "synthetic region pass failure");
        return false;
    }
    window->region_pass_count++;
    window->pass_source = source_target_id;
    window->pass_destination = destination_target_id;
    window->region_x = x;
    window->region_y = y;
    window->region_width = width;
    window->region_height = height;
    return true;
}

int main(void)
{
    BudoHost host;
    InputState input;
    Window window = {0};
    BudoGraphicsCapabilities capabilities = {0};
    BudoShaderProgram *program;
    BudoShaderProgram *file_program;
    BudoGpuBuffer *vertices;
    BudoGpuBuffer *indices;
    BudoTexture *texture;
    BudoTexture *file_texture;
    BudoTexture *encoded_texture;
    BudoMesh *mesh;
    BudoRenderTarget *target_a;
    BudoRenderTarget *target_b;
    BudoMeshDrawInfo draw = {0};
    float vertex_data[] = {0, 0, 1, 0, 0, 1};
    uint16_t index_data[] = {0, 1, 2};
    uint8_t pixels[16] = {0};
    uint8_t encoded[] = {1, 2, 3};
    uint8_t bad_encoded[] = {0xff};
    uint32_t texture_width = 0;
    uint32_t texture_height = 0;
    int32_t location = -1;

    input_init(&input);
    budo_native_host_init(&host, &input, NULL, NULL);
    budo_native_host_set_graphics_window(&host, &window);
    capabilities.struct_size = sizeof(capabilities);
    assert(budo_host_graphics_capabilities(&host, &capabilities));
    assert(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_MESH_DRAWING);
    assert(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_SHADER_FILES);
    assert(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_ENCODED_IMAGE_TEXTURES);
    assert(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_RENDER_TARGET_RGBA8);
    assert(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_FULLSCREEN_PASSES);
    assert(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_REGION_PASSES);
    assert(!budo_shader_program_create(&host, "x", 1, "x", 1));
    assert(budo_host_last_error(&host)->status == BUDO_STATUS_INVALID_STATE);

    budo_native_host_set_canvas(&host, (SkiaCanvas *)(uintptr_t)1);
    budo_native_host_surface_created(&host);
    budo_native_host_enter_callback(&host, true);
    assert(!budo_shader_program_create(&host, "fail", 4, "x", 1));
    assert(strstr(budo_host_last_error(&host)->message, "synthetic"));
    assert(!budo_shader_program_create_from_files(&host, "", "shader.frag"));
    assert(!budo_shader_program_create_from_files(&host, "missing.vert", "shader.frag"));
    assert(strstr(budo_host_last_error(&host)->message, "shader file"));
    program = budo_shader_program_create(&host, "vertex", 6, "fragment", 8);
    file_program = budo_shader_program_create_from_files(
        &host, "shader.vert", "shader.frag");
    vertices = budo_gpu_buffer_create(&host, BUDO_GPU_BUFFER_VERTEX,
                                      vertex_data, sizeof(vertex_data),
                                      BUDO_GPU_USAGE_DYNAMIC);
    indices = budo_gpu_buffer_create(&host, BUDO_GPU_BUFFER_INDEX,
                                     index_data, sizeof(index_data),
                                     BUDO_GPU_USAGE_STATIC);
    texture = budo_texture_2d_create_rgba8(&host, 2, 2, pixels, sizeof(pixels));
    assert(!budo_texture_2d_create_from_file(&host, "missing.png"));
    assert(strstr(budo_host_last_error(&host)->message, "image file"));
    assert(!budo_texture_2d_create_from_encoded_data(&host, bad_encoded,
                                                     sizeof(bad_encoded)));
    assert(strstr(budo_host_last_error(&host)->message, "decode"));
    file_texture = budo_texture_2d_create_from_file(&host, "image.png");
    encoded_texture = budo_texture_2d_create_from_encoded_data(
        &host, encoded, sizeof(encoded));
    mesh = budo_mesh_create(&host);
    assert(!budo_render_target_create_rgba8(&host, 0, 2));
    assert(!budo_render_target_create_rgba8(&host, 13, 2));
    assert(strstr(budo_host_last_error(&host)->message, "synthetic"));
    target_a = budo_render_target_create_rgba8(&host, 8, 6);
    target_b = budo_render_target_create_rgba8(&host, 4, 3);
    assert(program && file_program && vertices && indices && texture &&
           file_texture && encoded_texture && mesh && target_a && target_b);
    assert(strcmp(window.vertex_path, "shader.vert") == 0);
    assert(strcmp(window.fragment_path, "shader.frag") == 0);
    assert(budo_texture_2d_get_size(file_texture, &texture_width,
                                    &texture_height) == BUDO_STATUS_OK);
    assert(texture_width == 4 && texture_height == 3);
    assert(budo_texture_2d_update_rgba8(file_texture, 3, 2, 2, 2,
                                        pixels, sizeof(pixels)) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(window.last_size == sizeof(index_data));
    assert(budo_shader_program_attribute_location(program, "position", &location) == BUDO_STATUS_OK);
    assert(location == 3);
    assert(budo_mesh_set_vertex_buffer(mesh, 3, vertices, 2,
                                       BUDO_VERTEX_ATTRIBUTE_FLOAT32,
                                       2 * sizeof(float), 0) == BUDO_STATUS_OK);
    assert(budo_mesh_set_index_buffer(mesh, indices, BUDO_INDEX_UINT16) == BUDO_STATUS_OK);
    assert(budo_gpu_buffer_destroy(vertices) == BUDO_STATUS_INVALID_STATE);
    assert(budo_texture_2d_update_rgba8(texture, 1, 1, 2, 2,
                                        pixels, sizeof(pixels)) == BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_shader_program_bind_texture(program, "image", texture, 0) == BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_shader_program_bind_texture(program, "image", texture, 1) == BUDO_STATUS_OK);
    draw.struct_size = sizeof(draw);
    draw.primitive = BUDO_PRIMITIVE_TRIANGLES;
    draw.count = 3;
    draw.blend = BUDO_BLEND_ALPHA;
    assert(budo_canvas_draw_mesh(&host.canvas, program, mesh, &draw) == BUDO_STATUS_OK);
    assert(window.draw_count == 1);
    assert(budo_render_target_get_size(target_a, &texture_width,
                                       &texture_height) == BUDO_STATUS_OK);
    assert(texture_width == 8 && texture_height == 6);
    assert(budo_render_target_resize(target_a, 10, 5) == BUDO_STATUS_OK);
    assert(window.resized_target > 0);
    assert(budo_render_target_resize(target_a, 13, 5) == BUDO_STATUS_INTERNAL_ERROR);
    assert(strstr(budo_host_last_error(&host)->message, "synthetic"));
    assert(budo_canvas_draw_fullscreen_pass(&host.canvas, program, NULL,
                                            target_a) == BUDO_STATUS_OK);
    assert(window.pass_source == 0 && window.pass_destination > 0);
    assert(budo_canvas_draw_fullscreen_pass(&host.canvas, program, target_a,
                                            target_b) == BUDO_STATUS_OK);
    assert(window.pass_source != 0 && window.pass_destination != 0);
    assert(budo_canvas_draw_fullscreen_pass(&host.canvas, program, target_b,
                                            NULL) == BUDO_STATUS_OK);
    assert(window.pass_destination == 0 && window.pass_count == 3);
    assert(budo_canvas_draw_fullscreen_pass(&host.canvas, program, target_a,
                                            target_a) == BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_canvas_draw_region_pass(&host.canvas, program, NULL, target_a,
                                        -3, 2, 11, 7) == BUDO_STATUS_OK);
    assert(window.pass_source == 0 && window.pass_destination > 0);
    assert(window.region_x == -3 && window.region_y == 2);
    assert(window.region_width == 11 && window.region_height == 7);
    assert(budo_canvas_draw_region_pass(&host.canvas, program, target_a,
                                        target_b, 1, -2, 3, 4) == BUDO_STATUS_OK);
    assert(window.pass_source != 0 && window.pass_destination != 0);
    assert(window.region_pass_count == 2);
    assert(budo_canvas_draw_region_pass(&host.canvas, program, target_a,
                                        target_a, 0, 0, 1, 1) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_canvas_draw_region_pass(&host.canvas, program, target_a,
                                        NULL, 0, 0, 0, 1) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_canvas_draw_region_pass(&host.canvas, program, target_a,
                                        NULL, 0, 0,
                                        (uint32_t)INT_MAX + 1u, 1) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_canvas_draw_region_pass(&host.canvas, program, target_a,
                                        NULL, 77, 0, 1, 1) ==
           BUDO_STATUS_APPLICATION_ERROR);
    assert(strstr(budo_host_last_error(&host)->message, "synthetic region"));
    assert(budo_render_target_destroy(target_b) == BUDO_STATUS_OK);
    assert(budo_render_target_destroy(target_a) == BUDO_STATUS_OK);
    assert(budo_mesh_destroy(mesh) == BUDO_STATUS_OK);
    assert(budo_texture_destroy(encoded_texture) == BUDO_STATUS_OK);
    assert(budo_texture_destroy(file_texture) == BUDO_STATUS_OK);
    assert(budo_texture_destroy(texture) == BUDO_STATUS_OK);
    assert(budo_gpu_buffer_destroy(indices) == BUDO_STATUS_OK);
    assert(budo_gpu_buffer_destroy(vertices) == BUDO_STATUS_OK);
    assert(budo_shader_program_destroy(program) == BUDO_STATUS_OK);
    assert(budo_shader_program_destroy(file_program) == BUDO_STATUS_OK);
    assert(window.destroyed_textures == 3);
    assert(window.destroyed_targets == 2);
    budo_native_host_leave_callback(&host);
    budo_native_host_context_lost(&host);
    budo_native_host_reset(&host);
    puts("native_gpu_contract_test: ok");
    return 0;
}