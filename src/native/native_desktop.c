#include "native_application_driver.h"
#include "native_host_internal.h"
#include "desktop/desktop_host.h"

#include <stdio.h>

extern const BudoApplication *budo_get_application(void);

typedef struct NativeDesktopApplication
{
    BudoHost host;
    NativeApplicationDriver adapter;
    ApplicationDriver driver;
} NativeDesktopApplication;

static ApplicationDriver *native_get_driver(void *context)
{
    NativeDesktopApplication *application = context;
    return &application->driver;
}

static void native_before_frame(void *context, Window *window,
                                InputState *input, double now_seconds)
{
    NativeDesktopApplication *application = context;
    (void)input;
    (void)now_seconds;
    budo_native_host_set_canvas(&application->host, window_get_canvas(window));
}

static bool native_keep_running(void *context)
{
    NativeDesktopApplication *application = context;
    return !budo_native_host_exit_requested(&application->host, NULL);
}

static int native_run_without_window(NativeDesktopApplication *application)
{
    int exit_code = 0;
    int result;

    if (!application_driver_initialize(&application->driver))
    {
        const BudoError *error = budo_host_last_error(&application->host);
        fprintf(stderr, "Budo native application error: %s\n",
                error && error->message ? error->message : "initialization failed");
        result = 1;
    }
    else
    {
        budo_native_host_exit_requested(&application->host, &exit_code);
        result = exit_code;
    }
    application_driver_destroy(&application->driver);
    budo_native_host_reset(&application->host);
    return result;
}

int budo_native_desktop_main(int argc, char **argv)
{
    const BudoApplication *descriptor;
    NativeDesktopApplication application;
    DesktopHostApplication desktop_application;
    WindowConfig window_config;
    Window *window;
    InputState input;
    BudoStatus status;
    int width;
    int height;
    int result;

    (void)argc;
    (void)argv;
    descriptor = budo_get_application();
    input_init(&input);
    budo_native_host_init(&application.host, &input, NULL, NULL);
    status = native_application_driver_init(&application.driver,
                                            &application.adapter,
                                            descriptor,
                                            &application.host);
    if (status != BUDO_STATUS_OK)
    {
        const BudoError *error = budo_host_last_error(&application.host);
        fprintf(stderr, "Budo native application error: %s\n",
                error && error->message ? error->message : "invalid descriptor");
        budo_native_host_reset(&application.host);
        return 1;
    }
    if (!descriptor->frame)
        return native_run_without_window(&application);

    window_config.title = descriptor->name ? descriptor->name : "Budo Native";
    window_config.project_dir = ".";
    window_config.width = 800;
    window_config.height = 600;
    window_config.resizable = true;
    window_config.fullscreen = false;
    window_config.vsync = true;
    window = window_create(&window_config);
    if (!window)
    {
        fprintf(stderr, "Budo native application error: failed to create window\n");
        application_driver_destroy(&application.driver);
        budo_native_host_reset(&application.host);
        return 1;
    }
    budo_native_host_set_graphics_window(&application.host, window);

    if (!application_driver_initialize(&application.driver))
    {
        const BudoError *error = budo_host_last_error(&application.host);
        fprintf(stderr, "Budo native application error: %s\n",
                error && error->message ? error->message : "initialization failed");
        application_driver_destroy(&application.driver);
        budo_native_host_set_graphics_window(&application.host, NULL);
        window_destroy(window);
        budo_native_host_reset(&application.host);
        return 1;
    }
    budo_native_host_set_canvas(&application.host, window_get_canvas(window));
    application_driver_surface_created(&application.driver);
    window_get_size(window, &width, &height);
    application_driver_resize(&application.driver, width, height,
                              window_get_dpi_scale(window));

    desktop_application.context = &application;
    desktop_application.get_driver = native_get_driver;
    desktop_application.before_frame = native_before_frame;
    desktop_application.keep_running = native_keep_running;
    result = desktop_host_run(window, &input, &desktop_application);
    budo_native_host_exit_requested(&application.host, &result);

    application_driver_context_lost(&application.driver);
    application_driver_destroy(&application.driver);
    budo_native_host_set_graphics_window(&application.host, NULL);
    window_destroy(window);
    budo_native_host_reset(&application.host);
    return result;
}