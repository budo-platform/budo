#include "native_host_internal.h"
#include "graphics/skia_wrapper.h"

#include <math.h>
#include <stdlib.h>

struct BudoPaint
{
    BudoHost *host;
    SkiaPaint *implementation;
    uint64_t generation;
};

struct BudoPath
{
    BudoHost *host;
    SkiaPath *implementation;
    uint64_t generation;
};

static BudoStatus graphics_error(BudoHost *host, BudoStatus status,
                                 const char *message)
{
    budo_host_set_error(host, status, message);
    return status;
}

static bool finite_values(const float *values, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
    {
        if (!isfinite(values[i]))
            return false;
    }
    return true;
}

static BudoStatus validate_canvas(BudoCanvas *canvas)
{
    if (!canvas || !canvas->host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    if (!budo_native_host_graphics_available(canvas->host) ||
        canvas->implementation != canvas->host->canvas.implementation)
        return graphics_error(canvas->host, BUDO_STATUS_INVALID_STATE,
                              "Canvas drawing is only available during an active graphics callback");
    return BUDO_STATUS_OK;
}

static BudoStatus validate_paint(BudoHost *host, BudoPaint *paint)
{
    if (!paint || !paint->implementation)
        return graphics_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Paint handle is missing");
    if (paint->host != host)
        return graphics_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Paint belongs to a different Budo host");
    if (paint->generation != host->graphics_generation)
        return graphics_error(host, BUDO_STATUS_INVALID_STATE,
                              "Paint belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

static BudoStatus validate_path(BudoHost *host, BudoPath *path)
{
    if (!path || !path->implementation)
        return graphics_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Path handle is missing");
    if (path->host != host)
        return graphics_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Path belongs to a different Budo host");
    if (path->generation != host->graphics_generation)
        return graphics_error(host, BUDO_STATUS_INVALID_STATE,
                              "Path belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

static BudoStatus validate_resource_callback(BudoHost *host)
{
    if (!host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    if (!budo_native_host_graphics_available(host))
        return graphics_error(host, BUDO_STATUS_INVALID_STATE,
                              "Graphics resources require an active graphics callback");
    return BUDO_STATUS_OK;
}

BudoCanvas *budo_host_canvas(BudoHost *host)
{
    if (validate_resource_callback(host) != BUDO_STATUS_OK)
        return NULL;
    return &host->canvas;
}

BudoPaint *budo_paint_create(BudoHost *host)
{
    BudoPaint *paint;

    if (validate_resource_callback(host) != BUDO_STATUS_OK)
        return NULL;
    paint = calloc(1, sizeof(*paint));
    if (!paint)
    {
        graphics_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                       "Could not allocate paint handle");
        return NULL;
    }
    paint->implementation = skia_paint_create();
    if (!paint->implementation)
    {
        free(paint);
        graphics_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                       "Could not create paint resource");
        return NULL;
    }
    paint->host = host;
    paint->generation = host->graphics_generation;
    return paint;
}

void budo_paint_destroy(BudoPaint *paint)
{
    if (!paint)
        return;
    skia_paint_destroy(paint->implementation);
    paint->implementation = NULL;
    free(paint);
}

#define PAINT_SETTER(name, type, validation, skia_name, cast_type)           \
    BudoStatus name(BudoPaint *paint, type value)                            \
    {                                                                        \
        BudoStatus status;                                                   \
        if (!paint || !paint->host)                                          \
            return BUDO_STATUS_INVALID_ARGUMENT;                             \
        status = validate_resource_callback(paint->host);                    \
        if (status != BUDO_STATUS_OK)                                        \
            return status;                                                   \
        status = validate_paint(paint->host, paint);                         \
        if (status != BUDO_STATUS_OK)                                        \
            return status;                                                   \
        if (!(validation))                                                   \
            return graphics_error(paint->host, BUDO_STATUS_INVALID_ARGUMENT, \
                                  "Invalid paint property");                 \
        skia_name(paint->implementation, (cast_type)value);                  \
        return BUDO_STATUS_OK;                                               \
    }

PAINT_SETTER(budo_paint_set_color, BudoColor, true, skia_paint_set_color,
             SkiaColor)
PAINT_SETTER(budo_paint_set_style, BudoPaintStyle,
             value >= BUDO_PAINT_FILL && value <= BUDO_PAINT_STROKE_AND_FILL,
             skia_paint_set_style, SkiaPaintStyle)
PAINT_SETTER(budo_paint_set_stroke_width, float, isfinite(value) && value >= 0,
             skia_paint_set_stroke_width, float)
PAINT_SETTER(budo_paint_set_anti_alias, bool, true,
             skia_paint_set_anti_alias, bool)
PAINT_SETTER(budo_paint_set_stroke_cap, BudoStrokeCap,
             value >= BUDO_STROKE_CAP_BUTT && value <= BUDO_STROKE_CAP_SQUARE,
             skia_paint_set_stroke_cap, SkiaStrokeCap)
PAINT_SETTER(budo_paint_set_stroke_join, BudoStrokeJoin,
             value >= BUDO_STROKE_JOIN_MITER && value <= BUDO_STROKE_JOIN_BEVEL,
             skia_paint_set_stroke_join, SkiaStrokeJoin)
PAINT_SETTER(budo_paint_set_alpha, uint8_t, true, skia_paint_set_alpha, uint8_t)

BudoPath *budo_path_create(BudoHost *host)
{
    BudoPath *path;

    if (validate_resource_callback(host) != BUDO_STATUS_OK)
        return NULL;
    path = calloc(1, sizeof(*path));
    if (!path)
    {
        graphics_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                       "Could not allocate path handle");
        return NULL;
    }
    path->implementation = skia_path_create();
    if (!path->implementation)
    {
        free(path);
        graphics_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                       "Could not create path resource");
        return NULL;
    }
    path->host = host;
    path->generation = host->graphics_generation;
    return path;
}

void budo_path_destroy(BudoPath *path)
{
    if (!path)
        return;
    skia_path_destroy(path->implementation);
    path->implementation = NULL;
    free(path);
}

static BudoStatus prepare_path(BudoPath *path, const float *values,
                               size_t value_count)
{
    BudoStatus status;
    if (!path || !path->host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_resource_callback(path->host);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_path(path->host, path);
    if (status != BUDO_STATUS_OK)
        return status;
    if (values && !finite_values(values, value_count))
        return graphics_error(path->host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Path coordinates must be finite");
    return BUDO_STATUS_OK;
}

BudoStatus budo_path_reset(BudoPath *path)
{
    BudoStatus status = prepare_path(path, NULL, 0);
    if (status == BUDO_STATUS_OK)
        skia_path_reset(path->implementation);
    return status;
}

BudoStatus budo_path_move_to(BudoPath *path, float x, float y)
{
    float values[] = {x, y};
    BudoStatus status = prepare_path(path, values, 2);
    if (status == BUDO_STATUS_OK)
        skia_path_move_to(path->implementation, x, y);
    return status;
}

BudoStatus budo_path_line_to(BudoPath *path, float x, float y)
{
    float values[] = {x, y};
    BudoStatus status = prepare_path(path, values, 2);
    if (status == BUDO_STATUS_OK)
        skia_path_line_to(path->implementation, x, y);
    return status;
}

BudoStatus budo_path_quad_to(BudoPath *path, float x1, float y1,
                             float x2, float y2)
{
    float values[] = {x1, y1, x2, y2};
    BudoStatus status = prepare_path(path, values, 4);
    if (status == BUDO_STATUS_OK)
        skia_path_quad_to(path->implementation, x1, y1, x2, y2);
    return status;
}

BudoStatus budo_path_cubic_to(BudoPath *path, float x1, float y1,
                              float x2, float y2, float x3, float y3)
{
    float values[] = {x1, y1, x2, y2, x3, y3};
    BudoStatus status = prepare_path(path, values, 6);
    if (status == BUDO_STATUS_OK)
        skia_path_cubic_to(path->implementation, x1, y1, x2, y2, x3, y3);
    return status;
}

BudoStatus budo_path_close(BudoPath *path)
{
    BudoStatus status = prepare_path(path, NULL, 0);
    if (status == BUDO_STATUS_OK)
        skia_path_close(path->implementation);
    return status;
}

static BudoStatus prepare_draw(BudoCanvas *canvas, BudoPaint *paint,
                               const float *values, size_t value_count)
{
    BudoStatus status = validate_canvas(canvas);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_paint(canvas->host, paint);
    if (status != BUDO_STATUS_OK)
        return status;
    if (values && !finite_values(values, value_count))
        return graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Canvas coordinates must be finite");
    return BUDO_STATUS_OK;
}

BudoStatus budo_canvas_clear(BudoCanvas *canvas, BudoColor color)
{
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK)
        skia_canvas_clear(canvas->implementation, color);
    return status;
}

#define DRAW_RECT_FUNCTION(name, skia_name)                                   \
    BudoStatus name(BudoCanvas *canvas, float x, float y, float width,        \
                    float height, BudoPaint *paint)                           \
    {                                                                         \
        float values[] = {x, y, width, height};                               \
        BudoStatus status = prepare_draw(canvas, paint, values, 4);           \
        if (status != BUDO_STATUS_OK)                                         \
            return status;                                                    \
        if (width < 0 || height < 0)                                          \
            return graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT, \
                                  "Canvas dimensions cannot be negative");    \
        skia_name(canvas->implementation, x, y, x + width, y + height,        \
                  paint->implementation);                                     \
        return BUDO_STATUS_OK;                                                \
    }

DRAW_RECT_FUNCTION(budo_canvas_draw_rect, skia_canvas_draw_rect)
DRAW_RECT_FUNCTION(budo_canvas_draw_oval, skia_canvas_draw_oval)

BudoStatus budo_canvas_draw_round_rect(BudoCanvas *canvas, float x, float y,
                                       float width, float height,
                                       float radius_x, float radius_y,
                                       BudoPaint *paint)
{
    float values[] = {x, y, width, height, radius_x, radius_y};
    BudoStatus status = prepare_draw(canvas, paint, values, 6);
    if (status != BUDO_STATUS_OK)
        return status;
    if (width < 0 || height < 0 || radius_x < 0 || radius_y < 0)
        return graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Canvas dimensions and radii cannot be negative");
    skia_canvas_draw_round_rect(canvas->implementation, x, y, x + width,
                                y + height, radius_x, radius_y,
                                paint->implementation);
    return BUDO_STATUS_OK;
}

BudoStatus budo_canvas_draw_circle(BudoCanvas *canvas, float center_x,
                                   float center_y, float radius,
                                   BudoPaint *paint)
{
    float values[] = {center_x, center_y, radius};
    BudoStatus status = prepare_draw(canvas, paint, values, 3);
    if (status != BUDO_STATUS_OK)
        return status;
    if (radius < 0)
        return graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                              "Circle radius cannot be negative");
    skia_canvas_draw_circle(canvas->implementation, center_x, center_y, radius,
                            paint->implementation);
    return BUDO_STATUS_OK;
}

BudoStatus budo_canvas_draw_line(BudoCanvas *canvas, float x1, float y1,
                                 float x2, float y2, BudoPaint *paint)
{
    float values[] = {x1, y1, x2, y2};
    BudoStatus status = prepare_draw(canvas, paint, values, 4);
    if (status == BUDO_STATUS_OK)
        skia_canvas_draw_line(canvas->implementation, x1, y1, x2, y2,
                              paint->implementation);
    return status;
}

BudoStatus budo_canvas_draw_path(BudoCanvas *canvas, BudoPath *path,
                                 BudoPaint *paint)
{
    BudoStatus status = prepare_draw(canvas, paint, NULL, 0);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_path(canvas->host, path);
    if (status == BUDO_STATUS_OK)
        skia_canvas_draw_path(canvas->implementation, path->implementation,
                              paint->implementation);
    return status;
}

BudoStatus budo_canvas_draw_point(BudoCanvas *canvas, float x, float y,
                                  BudoPaint *paint)
{
    float values[] = {x, y};
    BudoStatus status = prepare_draw(canvas, paint, values, 2);
    if (status == BUDO_STATUS_OK)
        skia_canvas_draw_point(canvas->implementation, x, y,
                               paint->implementation);
    return status;
}

BudoStatus budo_canvas_save(BudoCanvas *canvas)
{
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK)
        skia_canvas_save(canvas->implementation);
    return status;
}

BudoStatus budo_canvas_restore(BudoCanvas *canvas)
{
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK)
        skia_canvas_restore(canvas->implementation);
    return status;
}

BudoStatus budo_canvas_translate(BudoCanvas *canvas, float x, float y)
{
    float values[] = {x, y};
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK && !finite_values(values, 2))
        status = graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Translation must be finite");
    if (status == BUDO_STATUS_OK)
        skia_canvas_translate(canvas->implementation, x, y);
    return status;
}

BudoStatus budo_canvas_scale(BudoCanvas *canvas, float x, float y)
{
    float values[] = {x, y};
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK && !finite_values(values, 2))
        status = graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Scale must be finite");
    if (status == BUDO_STATUS_OK)
        skia_canvas_scale(canvas->implementation, x, y);
    return status;
}

BudoStatus budo_canvas_rotate(BudoCanvas *canvas, float degrees)
{
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK && !isfinite(degrees))
        status = graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Rotation must be finite");
    if (status == BUDO_STATUS_OK)
        skia_canvas_rotate(canvas->implementation, degrees);
    return status;
}

BudoStatus budo_canvas_clip_rect(BudoCanvas *canvas, float x, float y,
                                 float width, float height)
{
    float values[] = {x, y, width, height};
    BudoStatus status = validate_canvas(canvas);
    if (status == BUDO_STATUS_OK && !finite_values(values, 4))
        status = graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Clip rectangle must be finite");
    if (status == BUDO_STATUS_OK && (width < 0 || height < 0))
        status = graphics_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                                "Clip dimensions cannot be negative");
    if (status == BUDO_STATUS_OK)
        skia_canvas_clip_rect(canvas->implementation, x, y, x + width,
                              y + height);
    return status;
}