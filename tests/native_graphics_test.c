#include <budo/budo.h>
#include "core/application_driver.h"
#include "core/input.h"
#include "graphics/skia_wrapper.h"
#include "native/native_application_driver.h"
#include "native/native_host_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static BudoPaint *paint;
static BudoPath *path;
static int surface_count;
static int frame_count;
static int lost_count;

static BudoStatus initialize(BudoHost *host, void **state)
{
    assert(budo_host_canvas(host) == NULL);
    assert(budo_host_last_error(host)->status == BUDO_STATUS_INVALID_STATE);
    *state = &surface_count;
    return BUDO_STATUS_OK;
}

static void surface_created(BudoHost *host, void *state)
{
    assert(state == &surface_count);
    assert(budo_host_canvas(host));
    paint = budo_paint_create(host);
    path = budo_path_create(host);
    assert(paint && path);
    assert(budo_paint_set_color(paint, BUDO_COLOR_RED) == BUDO_STATUS_OK);
    assert(budo_paint_set_anti_alias(paint, true) == BUDO_STATUS_OK);
    assert(budo_path_move_to(path, 5, 5) == BUDO_STATUS_OK);
    assert(budo_path_line_to(path, 40, 5) == BUDO_STATUS_OK);
    assert(budo_path_line_to(path, 20, 30) == BUDO_STATUS_OK);
    assert(budo_path_close(path) == BUDO_STATUS_OK);
    surface_count++;
}

static void frame(BudoHost *host, void *state, const BudoFrameInfo *info)
{
    BudoCanvas *canvas = budo_host_canvas(host);
    (void)state;
    (void)info;
    assert(canvas && paint && path);
    assert(budo_canvas_clear(canvas, BUDO_COLOR_BLACK) == BUDO_STATUS_OK);
    assert(budo_canvas_draw_rect(canvas, 8, 8, 40, 30, paint) ==
           BUDO_STATUS_OK);
    assert(budo_canvas_draw_circle(canvas, 64, 32, 12, paint) ==
           BUDO_STATUS_OK);
    assert(budo_canvas_draw_path(canvas, path, paint) == BUDO_STATUS_OK);

    const BudoColor colors[] = {BUDO_COLOR_RED, BUDO_COLOR_BLACK};
    const float stops[] = {0.0f, 1.0f};
    assert(budo_paint_set_linear_gradient(paint, 0, 0, 96, 0, colors, NULL, 2) ==
           BUDO_STATUS_OK);
    assert(budo_paint_set_radial_gradient(paint, 48, 32, 20, colors, stops, 2) ==
           BUDO_STATUS_OK);
    assert(budo_paint_set_sweep_gradient(paint, 48, 32, colors, NULL, 2) ==
           BUDO_STATUS_OK);
    assert(budo_paint_set_linear_gradient(paint, 0, 0, 1, 0, colors, NULL, 1) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_paint_set_radial_gradient(paint, 0, 0, 0, colors, NULL, 2) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_canvas_save(canvas) == BUDO_STATUS_OK);
    assert(budo_canvas_clip_round_rect(canvas, 50, 40, 40, 20, 6, 6) ==
           BUDO_STATUS_OK);
    assert(budo_canvas_clip_round_rect(canvas, 0, 0, -1, 4, 0, 0) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    assert(budo_canvas_clip_path(canvas, path) == BUDO_STATUS_OK);
    assert(budo_canvas_clip_path(canvas, NULL) != BUDO_STATUS_OK);
    assert(budo_canvas_save_layer(canvas, 128) == BUDO_STATUS_OK);
    assert(budo_canvas_draw_rect(canvas, 0, 0, 96, 64, paint) == BUDO_STATUS_OK);
    assert(budo_canvas_restore(canvas) == BUDO_STATUS_OK);
    assert(budo_canvas_save_layer_bounds(canvas, 0, 0, 10, 10, 255, 3) ==
           BUDO_STATUS_OK);
    assert(budo_canvas_restore(canvas) == BUDO_STATUS_OK);
    assert(budo_canvas_restore(canvas) == BUDO_STATUS_OK);
    assert(budo_paint_clear_gradient(paint) == BUDO_STATUS_OK);
    assert(budo_paint_set_color(paint, BUDO_COLOR_RED) == BUDO_STATUS_OK);
    frame_count++;
}

static void context_lost(BudoHost *host, void *state)
{
    BudoCanvas *canvas = budo_host_canvas(host);
    (void)state;
    assert(canvas);
    assert(budo_canvas_draw_point(canvas, 1, 1, paint) == BUDO_STATUS_OK);
    budo_path_destroy(path);
    budo_paint_destroy(paint);
    path = NULL;
    paint = NULL;
    lost_count++;
}

static const BudoApplication application = {
    .struct_size = sizeof(BudoApplication),
    .api_version = BUDO_NATIVE_API_VERSION,
    .sdk_version = BUDO_VERSION_STRING,
    .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID,
    .name = "Graphics contract test",
    .initialize = initialize,
    .surface_created = surface_created,
    .frame = frame,
    .context_lost = context_lost,
};

static void verify_surface_has_drawing(SkiaSurface *surface)
{
    uint32_t *pixels = malloc(96u * 64u * sizeof(*pixels));
    size_t i;
    bool found_non_background = false;
    assert(pixels && skia_surface_read_pixels(surface, pixels));
    for (i = 1; i < 96u * 64u; ++i)
    {
        if (pixels[i] != pixels[0])
        {
            found_non_background = true;
            break;
        }
    }
    free(pixels);
    assert(found_non_background);
}

int main(void)
{
    ApplicationDriver driver;
    NativeApplicationDriver adapter;
    BudoHost host;
    InputState input;
    SkiaSurface *first;
    SkiaSurface *second;
    BudoStatus init_status;
    bool initialized;

    input_init(&input);
    budo_native_host_init(&host, &input, NULL, NULL);
    init_status = native_application_driver_init(&driver, &adapter, &application,
                                                 &host);
    assert(init_status == BUDO_STATUS_OK);
    if (init_status != BUDO_STATUS_OK)
        return 1;
    initialized = application_driver_initialize(&driver);
    assert(initialized);
    if (!initialized)
        return 1;

    first = skia_surface_create_raster(96, 64);
    assert(first);
    budo_native_host_set_canvas(&host, skia_surface_get_canvas(first));
    application_driver_surface_created(&driver);
    application_driver_resize(&driver, 96, 64, 1.0f);
    application_driver_frame(&driver, 16.0, 0.016);
    verify_surface_has_drawing(first);
    assert(budo_host_canvas(&host) == NULL);
    application_driver_context_lost(&driver);
    assert(budo_host_canvas(&host) == NULL);
    skia_surface_destroy(first);

    second = skia_surface_create_raster(96, 64);
    assert(second);
    budo_native_host_set_canvas(&host, skia_surface_get_canvas(second));
    application_driver_surface_created(&driver);
    application_driver_resize(&driver, 96, 64, 2.0f);
    application_driver_frame(&driver, 32.0, 0.016);
    verify_surface_has_drawing(second);
    application_driver_context_lost(&driver);
    application_driver_destroy(&driver);
    skia_surface_destroy(second);

    assert(surface_count == 2);
    assert(frame_count == 2);
    assert(lost_count == 2);
    puts("native_graphics_test: ok");
    return 0;
}