#ifndef SKIA_WRAPPER_H
#define SKIA_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct SkiaCanvas SkiaCanvas;
    typedef struct SkiaSurface SkiaSurface;
    typedef struct SkiaPaint SkiaPaint;
    typedef struct SkiaPath SkiaPath;
    typedef struct SkiaFont SkiaFont;

    typedef struct
    {
        SkiaCanvas *canvas;
        SkiaPaint *active_paint;
        SkiaFont *active_font;
    } SkiaDrawingDesk;

    typedef uint32_t SkiaColor;

#define SKIA_COLOR_ARGB(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define SKIA_COLOR_RGB(r, g, b) SKIA_COLOR_ARGB(255, r, g, b)

#define SKIA_COLOR_BLACK SKIA_COLOR_RGB(0, 0, 0)
#define SKIA_COLOR_WHITE SKIA_COLOR_RGB(255, 255, 255)
#define SKIA_COLOR_RED SKIA_COLOR_RGB(255, 0, 0)
#define SKIA_COLOR_GREEN SKIA_COLOR_RGB(0, 255, 0)
#define SKIA_COLOR_BLUE SKIA_COLOR_RGB(0, 0, 255)
#define SKIA_COLOR_TRANSPARENT SKIA_COLOR_ARGB(0, 0, 0, 0)

#define BUDO_DEFAULT_FONT_SIZE 32.0

    typedef enum
    {
        SKIA_PAINT_FILL = 0,
        SKIA_PAINT_STROKE = 1,
        SKIA_PAINT_STROKE_AND_FILL = 2
    } SkiaPaintStyle;

    typedef enum
    {
        SKIA_STROKE_CAP_BUTT = 0,
        SKIA_STROKE_CAP_ROUND = 1,
        SKIA_STROKE_CAP_SQUARE = 2
    } SkiaStrokeCap;

    typedef enum
    {
        SKIA_STROKE_JOIN_MITER = 0,
        SKIA_STROKE_JOIN_ROUND = 1,
        SKIA_STROKE_JOIN_BEVEL = 2
    } SkiaStrokeJoin;

    typedef struct
    {
        float left;
        float top;
        float right;
        float bottom;
    } SkiaRect;

    typedef struct
    {
        float x;
        float y;
    } SkiaPoint;

    SkiaSurface *skia_surface_create_raster(int width, int height);

    void skia_surface_destroy(SkiaSurface *surface);

    SkiaCanvas *skia_surface_get_canvas(SkiaSurface *surface);

    bool skia_surface_read_pixels(SkiaSurface *surface, void *pixels);

    void skia_surface_get_size(SkiaSurface *surface, int *width, int *height);

    SkiaPaint *skia_paint_create(void);

    void skia_paint_destroy(SkiaPaint *paint);

    void skia_paint_set_color(SkiaPaint *paint, SkiaColor color);

    SkiaColor skia_paint_get_color(SkiaPaint *paint);

    void skia_paint_set_style(SkiaPaint *paint, SkiaPaintStyle style);

    void skia_paint_set_stroke_width(SkiaPaint *paint, float width);

    float skia_paint_get_stroke_width(SkiaPaint *paint);

    void skia_paint_set_anti_alias(SkiaPaint *paint, bool enabled);

    void skia_paint_set_stroke_cap(SkiaPaint *paint, SkiaStrokeCap cap);

    void skia_paint_set_stroke_join(SkiaPaint *paint, SkiaStrokeJoin join);

    void skia_paint_set_alpha(SkiaPaint *paint, uint8_t alpha);

    SkiaPath *skia_path_create(void);

    void skia_path_destroy(SkiaPath *path);

    void skia_path_reset(SkiaPath *path);

    void skia_path_move_to(SkiaPath *path, float x, float y);

    void skia_path_line_to(SkiaPath *path, float x, float y);

    void skia_path_quad_to(SkiaPath *path, float x1, float y1, float x2, float y2);

    void skia_path_cubic_to(SkiaPath *path, float x1, float y1, float x2, float y2, float x3, float y3);

    void skia_path_close(SkiaPath *path);

    void skia_path_add_rect(SkiaPath *path, float left, float top, float right, float bottom);

    void skia_path_add_oval(SkiaPath *path, float left, float top, float right, float bottom);

    void skia_path_add_circle(SkiaPath *path, float cx, float cy, float radius);

    bool skia_path_add_svg(SkiaPath *path, const char *data);

    void skia_path_add_arc(SkiaPath *path, float left, float top, float right, float bottom,
                           float start_angle, float sweep_angle);

    void skia_canvas_clear(SkiaCanvas *canvas, SkiaColor color);

    void skia_canvas_draw_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom, SkiaPaint *paint);

    void skia_canvas_draw_round_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom,
                                     float rx, float ry, SkiaPaint *paint);

    void skia_canvas_draw_circle(SkiaCanvas *canvas, float cx, float cy, float radius, SkiaPaint *paint);

    void skia_canvas_draw_oval(SkiaCanvas *canvas, float left, float top, float right, float bottom, SkiaPaint *paint);

    void skia_canvas_draw_line(SkiaCanvas *canvas, float x1, float y1, float x2, float y2, SkiaPaint *paint);

    void skia_canvas_draw_path(SkiaCanvas *canvas, SkiaPath *path, SkiaPaint *paint);

    void skia_canvas_draw_point(SkiaCanvas *canvas, float x, float y, SkiaPaint *paint);

    SkiaFont *skia_font_load_file(const char *filename);

    SkiaFont *skia_font_load_buffer(const void *data, size_t size);

    void skia_font_destroy(SkiaFont *font);

    float skia_canvas_draw_text(SkiaCanvas *canvas, const char *text, float x, float y, float font_size, SkiaPaint *paint);

    float skia_canvas_draw_text_with_font(SkiaCanvas *canvas, const char *text, float x, float y, float font_size,
                                          SkiaPaint *paint, SkiaFont *font);

    float skia_measure_text_width(const char *text, float font_size, SkiaFont *font);

    void skia_measure_text_rect(const char *text, float font_size, SkiaFont *font, float *out_width, float *out_height);

    void skia_canvas_draw_arc(SkiaCanvas *canvas, float left, float top, float right, float bottom,
                              float start_angle, float sweep_angle, bool use_center, SkiaPaint *paint);

    void skia_canvas_save(SkiaCanvas *canvas);

    void skia_canvas_restore(SkiaCanvas *canvas);

    int skia_canvas_get_save_count(SkiaCanvas *canvas);

    void skia_canvas_restore_to_count(SkiaCanvas *canvas, int count);

    void skia_canvas_translate(SkiaCanvas *canvas, float dx, float dy);

    void skia_canvas_scale(SkiaCanvas *canvas, float sx, float sy);

    void skia_canvas_rotate(SkiaCanvas *canvas, float degrees);

    void skia_canvas_rotate_around(SkiaCanvas *canvas, float degrees, float px, float py);

    void skia_canvas_skew(SkiaCanvas *canvas, float sx, float sy);

    void skia_canvas_reset_transform(SkiaCanvas *canvas);

    void skia_canvas_clip_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom);

    void skia_canvas_clip_path(SkiaCanvas *canvas, SkiaPath *path);

    void skia_canvas_clip_round_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom,
                                     float rx, float ry);

    void skia_canvas_save_layer(SkiaCanvas *canvas, const SkiaRect *bounds, uint8_t alpha,
                                float backdrop_sigma);

#define SKIA_GRADIENT_MAX_STOPS 16

    bool skia_paint_set_linear_gradient(SkiaPaint *paint, float x0, float y0, float x1, float y1,
                                        const uint32_t *colors, const float *stops, int count);
    bool skia_paint_set_radial_gradient(SkiaPaint *paint, float cx, float cy, float radius,
                                        const uint32_t *colors, const float *stops, int count);
    
    bool skia_paint_set_sweep_gradient(SkiaPaint *paint, float cx, float cy,
                                       const uint32_t *colors, const float *stops, int count);
    void skia_paint_clear_shader(SkiaPaint *paint);

#define SKIA_DEFAULT_LINE_HEIGHT 1.25f

    typedef enum
    {
        SKIA_TEXT_ALIGN_LEFT = 0,
        SKIA_TEXT_ALIGN_CENTER = 1,
        SKIA_TEXT_ALIGN_RIGHT = 2
    } SkiaTextAlign;

    typedef struct
    {
        float width;  
        float height; 
        int lines;
    } SkiaParagraphMetrics;

    typedef struct
    {
        const char *text;
        float size;
        SkiaFont *font;
        SkiaColor color;
        bool has_color;
    } SkiaTextSpan;

    SkiaParagraphMetrics skia_measure_rich_text(const SkiaTextSpan *spans, int count, float max_width,
                                                float line_height, int max_lines);
    SkiaParagraphMetrics skia_canvas_draw_rich_text(SkiaCanvas *canvas, const SkiaTextSpan *spans, int count,
                                                    float x, float y, float max_width, float line_height,
                                                    SkiaTextAlign align, int max_lines, SkiaPaint *paint);

    SkiaParagraphMetrics skia_measure_paragraph(const char *text, float max_width, float font_size,
                                                float line_height, int max_lines, SkiaFont *font);
    SkiaParagraphMetrics skia_canvas_draw_paragraph(SkiaCanvas *canvas, const char *text, float x, float y,
                                                    float max_width, float font_size, float line_height,
                                                    SkiaTextAlign align, int max_lines,
                                                    SkiaPaint *paint, SkiaFont *font);

    SkiaCanvas *skia_canvas_create_gl(int width, int height);

    SkiaCanvas *skia_canvas_create_gl_offscreen(int width, int height,
                                                unsigned int *out_gl_texture_id,
                                                unsigned int *out_gl_fbo_id);

    void skia_canvas_destroy(SkiaCanvas *canvas);

    void skia_canvas_flush(SkiaCanvas *canvas);

    void skia_canvas_reset_gl_context(SkiaCanvas *canvas);

    bool skia_canvas_get_size(SkiaCanvas *canvas, int *out_width, int *out_height);

    bool skia_canvas_read_pixels(SkiaCanvas *canvas, void *out_pixels);

    typedef struct SkiaSVG SkiaSVG;

    SkiaSVG *skia_svg_load_file(const char *filename);

    SkiaSVG *skia_svg_load_buffer(const void *data, size_t size);

    void skia_svg_destroy(SkiaSVG *svg);

    float skia_svg_get_width(SkiaSVG *svg);

    float skia_svg_get_height(SkiaSVG *svg);

    void skia_svg_render(SkiaSVG *svg, SkiaCanvas *canvas, float x, float y, float width, float height);

    bool skia_image_draw_file(SkiaCanvas *canvas, const char *filename,
                              float x, float y, float width, float height);

    bool skia_surface_encode_png_to_file(SkiaSurface *surface,
                                         const char *filename);

    typedef enum
    {
        SKIA_BLEND_MODE_SRC_OVER = 0, 
        SKIA_BLEND_MODE_SRC = 1,
        SKIA_BLEND_MODE_DST_OVER = 2,
        SKIA_BLEND_MODE_DST_IN = 3,
        SKIA_BLEND_MODE_DST_OUT = 4,
        SKIA_BLEND_MODE_SRC_IN = 5,
        SKIA_BLEND_MODE_SRC_OUT = 6,
        SKIA_BLEND_MODE_CLEAR = 7,
        SKIA_BLEND_MODE_PLUS = 8,
        SKIA_BLEND_MODE_MULTIPLY = 9,
        SKIA_BLEND_MODE_SCREEN = 10,
        SKIA_BLEND_MODE_OVERLAY = 11,
        SKIA_BLEND_MODE_DARKEN = 12,
        SKIA_BLEND_MODE_LIGHTEN = 13,
        SKIA_BLEND_MODE_COLOR_DODGE = 14,
        SKIA_BLEND_MODE_COLOR_BURN = 15,
        SKIA_BLEND_MODE_HARD_LIGHT = 16,
        SKIA_BLEND_MODE_SOFT_LIGHT = 17,
        SKIA_BLEND_MODE_DIFFERENCE = 18,
        SKIA_BLEND_MODE_EXCLUSION = 19,
        SKIA_BLEND_MODE_HUE = 20,
        SKIA_BLEND_MODE_SATURATION = 21,
        SKIA_BLEND_MODE_COLOR = 22,
        SKIA_BLEND_MODE_LUMINOSITY = 23
    } SkiaBlendMode;

    void skia_paint_set_blend_mode(SkiaPaint *paint, SkiaBlendMode mode);

    void skia_paint_set_blur_filter(SkiaPaint *paint, float sigma_x, float sigma_y);

    void skia_paint_set_drop_shadow_filter(SkiaPaint *paint,
                                           float dx, float dy,
                                           float sigma_x, float sigma_y,
                                           uint32_t color_argb);

    void skia_paint_set_drop_shadow_only_filter(SkiaPaint *paint,
                                                float dx, float dy,
                                                float sigma_x, float sigma_y,
                                                uint32_t color_argb);

    void skia_paint_clear_image_filter(SkiaPaint *paint);

    void skia_paint_set_color_matrix_filter(SkiaPaint *paint, const float m[20]);

    void skia_paint_set_blend_color_filter(SkiaPaint *paint,
                                           uint32_t color_argb,
                                           SkiaBlendMode mode);

    void skia_paint_clear_color_filter(SkiaPaint *paint);

#ifdef __cplusplus
}
#endif

#endif