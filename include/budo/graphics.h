#ifndef BUDO_PUBLIC_GRAPHICS_H
#define BUDO_PUBLIC_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <budo/core.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct BudoCanvas BudoCanvas;
    typedef struct BudoPaint BudoPaint;
    typedef struct BudoPath BudoPath;
    typedef struct BudoShaderProgram BudoShaderProgram;
    typedef struct BudoGpuBuffer BudoGpuBuffer;
    typedef struct BudoTexture BudoTexture;
    typedef struct BudoMesh BudoMesh;
    typedef struct BudoRenderTarget BudoRenderTarget;
    typedef uint32_t BudoColor;

#define BUDO_COLOR_ARGB(a, r, g, b)                  \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | \
     ((uint32_t)(g) << 8) | (uint32_t)(b))
#define BUDO_COLOR_RGB(r, g, b) BUDO_COLOR_ARGB(255, r, g, b)
#define BUDO_COLOR_BLACK BUDO_COLOR_RGB(0, 0, 0)
#define BUDO_COLOR_WHITE BUDO_COLOR_RGB(255, 255, 255)
#define BUDO_COLOR_RED BUDO_COLOR_RGB(255, 0, 0)
#define BUDO_COLOR_GREEN BUDO_COLOR_RGB(0, 255, 0)
#define BUDO_COLOR_BLUE BUDO_COLOR_RGB(0, 0, 255)
#define BUDO_COLOR_TRANSPARENT BUDO_COLOR_ARGB(0, 0, 0, 0)

    typedef int32_t BudoPaintStyle;
    enum
    {
        BUDO_PAINT_FILL = 0,
        BUDO_PAINT_STROKE = 1,
        BUDO_PAINT_STROKE_AND_FILL = 2
    };

    typedef int32_t BudoStrokeCap;
    enum
    {
        BUDO_STROKE_CAP_BUTT = 0,
        BUDO_STROKE_CAP_ROUND = 1,
        BUDO_STROKE_CAP_SQUARE = 2
    };

    typedef int32_t BudoStrokeJoin;
    enum
    {
        BUDO_STROKE_JOIN_MITER = 0,
        BUDO_STROKE_JOIN_ROUND = 1,
        BUDO_STROKE_JOIN_BEVEL = 2
    };

    typedef uint64_t BudoGraphicsFeatureFlags;
    enum
    {
        BUDO_GRAPHICS_FEATURE_SHADER_PROGRAMS = UINT64_C(1) << 0,
        BUDO_GRAPHICS_FEATURE_VERTEX_BUFFERS = UINT64_C(1) << 1,
        BUDO_GRAPHICS_FEATURE_INDEX_BUFFERS = UINT64_C(1) << 2,
        BUDO_GRAPHICS_FEATURE_TEXTURE_2D_RGBA8 = UINT64_C(1) << 3,
        BUDO_GRAPHICS_FEATURE_MESH_DRAWING = UINT64_C(1) << 4,
        BUDO_GRAPHICS_FEATURE_SHADER_FILES = UINT64_C(1) << 5,
        BUDO_GRAPHICS_FEATURE_ENCODED_IMAGE_TEXTURES = UINT64_C(1) << 6,
        BUDO_GRAPHICS_FEATURE_RENDER_TARGET_RGBA8 = UINT64_C(1) << 7,
        BUDO_GRAPHICS_FEATURE_FULLSCREEN_PASSES = UINT64_C(1) << 8,
        BUDO_GRAPHICS_FEATURE_REGION_PASSES = UINT64_C(1) << 9
    };

    typedef struct BudoGraphicsCapabilities
    {
        uint32_t struct_size;
        BudoGraphicsFeatureFlags feature_flags;

        uint32_t max_texture_units;
        uint32_t max_vertex_attributes;
    } BudoGraphicsCapabilities;

    typedef int32_t BudoGpuBufferTarget;
    enum
    {
        BUDO_GPU_BUFFER_VERTEX = 0,
        BUDO_GPU_BUFFER_INDEX = 1
    };

    typedef int32_t BudoGpuBufferUsage;
    enum
    {
        BUDO_GPU_USAGE_STATIC = 0,
        BUDO_GPU_USAGE_DYNAMIC = 1
    };

    typedef int32_t BudoVertexAttributeType;
    enum
    {
        BUDO_VERTEX_ATTRIBUTE_FLOAT32 = 0,
        BUDO_VERTEX_ATTRIBUTE_UNORM8 = 1
    };

    typedef int32_t BudoIndexType;
    enum
    {
        BUDO_INDEX_UINT16 = 0
    };

    typedef int32_t BudoPrimitive;
    enum
    {
        BUDO_PRIMITIVE_TRIANGLES = 0,
        BUDO_PRIMITIVE_TRIANGLE_STRIP = 1,
        BUDO_PRIMITIVE_TRIANGLE_FAN = 2,
        BUDO_PRIMITIVE_LINES = 3,
        BUDO_PRIMITIVE_LINE_STRIP = 4,
        BUDO_PRIMITIVE_POINTS = 5
    };

    typedef int32_t BudoCullMode;
    enum
    {
        BUDO_CULL_NONE = 0,
        BUDO_CULL_BACK = 1,
        BUDO_CULL_FRONT = 2
    };

    typedef int32_t BudoBlendMode;
    enum
    {
        BUDO_BLEND_NONE = 0,
        BUDO_BLEND_ALPHA = 1,
        BUDO_BLEND_ADD = 2,
        BUDO_BLEND_PREMULTIPLIED_ALPHA = 3
    };

    typedef struct BudoMeshDrawInfo
    {
        uint32_t struct_size;
        BudoPrimitive primitive;
        uint32_t first;
        uint32_t count;
        bool depth_test;
        bool depth_write;
        BudoCullMode cull;
        BudoBlendMode blend;
    } BudoMeshDrawInfo;

    bool budo_host_graphics_capabilities(
        const BudoHost *host, BudoGraphicsCapabilities *out_capabilities);

    BudoCanvas *budo_host_canvas(BudoHost *host);

    BudoPaint *budo_paint_create(BudoHost *host);
    void budo_paint_destroy(BudoPaint *paint);
    BudoStatus budo_paint_set_color(BudoPaint *paint, BudoColor color);
    BudoStatus budo_paint_set_style(BudoPaint *paint, BudoPaintStyle style);
    BudoStatus budo_paint_set_stroke_width(BudoPaint *paint, float width);
    BudoStatus budo_paint_set_anti_alias(BudoPaint *paint, bool enabled);
    BudoStatus budo_paint_set_stroke_cap(BudoPaint *paint, BudoStrokeCap cap);
    BudoStatus budo_paint_set_stroke_join(BudoPaint *paint, BudoStrokeJoin join);
    BudoStatus budo_paint_set_alpha(BudoPaint *paint, uint8_t alpha);

#define BUDO_GRADIENT_MAX_STOPS 16
    BudoStatus budo_paint_set_linear_gradient(BudoPaint *paint, float x0, float y0,
                                              float x1, float y1,
                                              const BudoColor *colors,
                                              const float *stops, size_t count);
    BudoStatus budo_paint_set_radial_gradient(BudoPaint *paint, float center_x,
                                              float center_y, float radius,
                                              const BudoColor *colors,
                                              const float *stops, size_t count);
    
    BudoStatus budo_paint_set_sweep_gradient(BudoPaint *paint, float center_x,
                                             float center_y,
                                             const BudoColor *colors,
                                             const float *stops, size_t count);
    BudoStatus budo_paint_clear_gradient(BudoPaint *paint);

    BudoPath *budo_path_create(BudoHost *host);
    void budo_path_destroy(BudoPath *path);
    BudoStatus budo_path_reset(BudoPath *path);
    BudoStatus budo_path_move_to(BudoPath *path, float x, float y);
    BudoStatus budo_path_line_to(BudoPath *path, float x, float y);
    BudoStatus budo_path_quad_to(BudoPath *path, float x1, float y1,
                                 float x2, float y2);
    BudoStatus budo_path_cubic_to(BudoPath *path, float x1, float y1,
                                  float x2, float y2, float x3, float y3);
    BudoStatus budo_path_close(BudoPath *path);

    BudoStatus budo_canvas_clear(BudoCanvas *canvas, BudoColor color);
    BudoStatus budo_canvas_draw_rect(BudoCanvas *canvas, float x, float y,
                                     float width, float height,
                                     BudoPaint *paint);
    BudoStatus budo_canvas_draw_round_rect(BudoCanvas *canvas, float x, float y,
                                           float width, float height,
                                           float radius_x, float radius_y,
                                           BudoPaint *paint);
    BudoStatus budo_canvas_draw_circle(BudoCanvas *canvas, float center_x,
                                       float center_y, float radius,
                                       BudoPaint *paint);
    BudoStatus budo_canvas_draw_oval(BudoCanvas *canvas, float x, float y,
                                     float width, float height,
                                     BudoPaint *paint);
    BudoStatus budo_canvas_draw_line(BudoCanvas *canvas, float x1, float y1,
                                     float x2, float y2, BudoPaint *paint);
    BudoStatus budo_canvas_draw_path(BudoCanvas *canvas, BudoPath *path,
                                     BudoPaint *paint);
    BudoStatus budo_canvas_draw_point(BudoCanvas *canvas, float x, float y,
                                      BudoPaint *paint);
    BudoStatus budo_canvas_save(BudoCanvas *canvas);
    BudoStatus budo_canvas_restore(BudoCanvas *canvas);
    BudoStatus budo_canvas_translate(BudoCanvas *canvas, float x, float y);
    BudoStatus budo_canvas_scale(BudoCanvas *canvas, float x, float y);
    BudoStatus budo_canvas_rotate(BudoCanvas *canvas, float degrees);
    BudoStatus budo_canvas_clip_rect(BudoCanvas *canvas, float x, float y,
                                     float width, float height);
    
    BudoStatus budo_canvas_clip_round_rect(BudoCanvas *canvas, float x, float y,
                                           float width, float height,
                                           float radius_x, float radius_y);
    BudoStatus budo_canvas_clip_path(BudoCanvas *canvas, BudoPath *path);

    BudoStatus budo_canvas_save_layer(BudoCanvas *canvas, uint8_t alpha);
    BudoStatus budo_canvas_save_layer_bounds(BudoCanvas *canvas, float x, float y,
                                             float width, float height,
                                             uint8_t alpha, float backdrop_blur);

    BudoShaderProgram *budo_shader_program_create(
        BudoHost *host, const char *vertex_source, size_t vertex_length,
        const char *fragment_source, size_t fragment_length);

    BudoShaderProgram *budo_shader_program_create_from_files(
        BudoHost *host, const char *vertex_path, const char *fragment_path);
    BudoStatus budo_shader_program_destroy(BudoShaderProgram *program);
    BudoStatus budo_shader_program_attribute_location(
        BudoShaderProgram *program, const char *name, int32_t *out_location);
    BudoStatus budo_shader_program_set_uniform_1i(
        BudoShaderProgram *program, const char *name, int32_t value);
    BudoStatus budo_shader_program_set_uniform_1f(
        BudoShaderProgram *program, const char *name, float value);
    BudoStatus budo_shader_program_set_uniform_2f(
        BudoShaderProgram *program, const char *name, float x, float y);
    BudoStatus budo_shader_program_set_uniform_3f(
        BudoShaderProgram *program, const char *name, float x, float y,
        float z);
    BudoStatus budo_shader_program_set_uniform_4f(
        BudoShaderProgram *program, const char *name, float x, float y,
        float z, float w);
    BudoStatus budo_shader_program_set_uniform_matrix4(
        BudoShaderProgram *program, const char *name,
        const float values[16]);

    BudoGpuBuffer *budo_gpu_buffer_create(BudoHost *host,
                                          BudoGpuBufferTarget target,
                                          const void *data, size_t size,
                                          BudoGpuBufferUsage usage);
    BudoStatus budo_gpu_buffer_update(BudoGpuBuffer *buffer, size_t offset,
                                      const void *data, size_t size);
    BudoStatus budo_gpu_buffer_destroy(BudoGpuBuffer *buffer);

    BudoTexture *budo_texture_2d_create_rgba8(BudoHost *host, uint32_t width,
                                              uint32_t height,
                                              const void *pixels,
                                              size_t byte_count);
    BudoTexture *budo_texture_2d_create_from_file(BudoHost *host,
                                                  const char *path);

    BudoTexture *budo_texture_2d_create_from_encoded_data(
        BudoHost *host, const void *data, size_t byte_count);
    BudoStatus budo_texture_2d_get_size(BudoTexture *texture,
                                        uint32_t *out_width,
                                        uint32_t *out_height);
    BudoStatus budo_texture_2d_update_rgba8(BudoTexture *texture, uint32_t x,
                                            uint32_t y, uint32_t width,
                                            uint32_t height,
                                            const void *pixels,
                                            size_t byte_count);
    BudoStatus budo_texture_destroy(BudoTexture *texture);
    BudoStatus budo_shader_program_bind_texture(
        BudoShaderProgram *program, const char *uniform_name,
        
        BudoTexture *texture, uint32_t texture_unit);

    BudoRenderTarget *budo_render_target_create_rgba8(
        BudoHost *host, uint32_t width, uint32_t height);
    BudoStatus budo_render_target_resize(BudoRenderTarget *target,
                                         uint32_t width, uint32_t height);
    BudoStatus budo_render_target_get_size(BudoRenderTarget *target,
                                           uint32_t *out_width,
                                           uint32_t *out_height);
    BudoStatus budo_render_target_destroy(BudoRenderTarget *target);

    BudoStatus budo_canvas_draw_fullscreen_pass(
        BudoCanvas *canvas, BudoShaderProgram *program,
        BudoRenderTarget *source, BudoRenderTarget *destination);

    BudoStatus budo_canvas_draw_region_pass(
        BudoCanvas *canvas, BudoShaderProgram *program,
        BudoRenderTarget *source, BudoRenderTarget *destination,
        int32_t x, int32_t y, uint32_t width, uint32_t height);

    BudoMesh *budo_mesh_create(BudoHost *host);
    BudoStatus budo_mesh_set_vertex_buffer(
        BudoMesh *mesh, uint32_t location, BudoGpuBuffer *buffer,
        uint32_t component_count, BudoVertexAttributeType type,
        uint32_t stride, uint32_t offset);
    BudoStatus budo_mesh_set_index_buffer(BudoMesh *mesh,
                                          BudoGpuBuffer *buffer,
                                          BudoIndexType type);
    BudoStatus budo_mesh_destroy(BudoMesh *mesh);
    BudoStatus budo_canvas_draw_mesh(BudoCanvas *canvas,
                                     BudoShaderProgram *program,
                                     BudoMesh *mesh,
                                     const BudoMeshDrawInfo *draw_info);

#ifdef __cplusplus
}
#endif

#endif