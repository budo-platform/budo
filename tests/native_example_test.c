#include <budo/budo.h>
#include "core/application_driver.h"
#include "core/input.h"
#include "native/native_application_driver.h"
#include "native/native_host_internal.h"
#include "graphics/skia_wrapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LogCapture
{
    char messages[256];
} LogCapture;

static void capture_log(void *opaque, BudoLogLevel level, const char *message)
{
    LogCapture *capture = opaque;
    (void)level;
    if (capture->messages[0])
        strncat(capture->messages, "|",
                sizeof(capture->messages) - strlen(capture->messages) - 1);
    strncat(capture->messages, message,
            sizeof(capture->messages) - strlen(capture->messages) - 1);
}

int main(void)
{
    ApplicationDriver driver;
    NativeApplicationDriver adapter;
    BudoHost host;
    InputState input;
    SkiaSurface *surface;
    uint32_t *pixels;
    size_t pixel_count = 640u * 480u;
    size_t i;
    bool found_drawing = false;
    LogCapture capture = {{0}};
    const BudoApplication *application = budo_get_application();

    input_init(&input);
    input_set_mouse_position(&input, 12, 34);
    budo_native_host_init(&host, &input, capture_log, &capture);
    surface = skia_surface_create_raster(640, 480);
    assert(surface);
    budo_native_host_set_canvas(&host, skia_surface_get_canvas(surface));
    assert(native_application_driver_init(&driver, &adapter, application,
                                          &host) == BUDO_STATUS_OK);
    assert(application_driver_initialize(&driver));
    application_driver_surface_created(&driver);
    application_driver_resize(&driver, 640, 480, 1.0f);
    application_driver_frame(&driver, 20.0, 0.02);
    pixels = malloc(pixel_count * sizeof(*pixels));
    assert(pixels && skia_surface_read_pixels(surface, pixels));
    for (i = 1; i < pixel_count; ++i)
    {
        if (pixels[i] != pixels[0])
        {
            found_drawing = true;
            break;
        }
    }
    assert(found_drawing);
    free(pixels);
    application_driver_context_lost(&driver);
    application_driver_destroy(&driver);

    assert(strcmp(capture.messages,
                  "Native C example initialized|Native C example shut down") == 0);
    skia_surface_destroy(surface);
    puts("native_example_test: ok");
    return 0;
}