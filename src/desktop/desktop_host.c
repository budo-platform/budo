#include "desktop_host.h"

int desktop_host_run(Window *window, InputState *input,
                     const DesktopHostApplication *application)
{
    if (!window || !input || !application || !application->get_driver)
        return 1;

    for (;;)
    {

        double idle = application->idle_ms ? application->idle_ms(application->context) : -1.0;
        if (idle > 0.0)
            window_wait_events(window, idle < 100.0 ? (int)idle + 1 : 100);
        if (!window_poll_events(window, input))
            break;
        if (application->keep_running && !application->keep_running(application->context))
            break;
        ApplicationDriver *driver;
        double now = window_get_time(window);
        double timestamp_ms = now * 1000.0;
        int width;
        int height;

        window_begin_frame(window, now);
        if (application->before_frame)
            application->before_frame(application->context, window, input, now);

        driver = application->get_driver(application->context);
        if (driver)
        {
            window_get_size(window, &width, &height);
            application_driver_resize(driver, width, height,
                                      window_get_dpi_scale(window));
            application_driver_frame(driver, timestamp_ms,
                                     window_get_delta_time(window));
        }
        window_present(window);
    }

    return 0;
}