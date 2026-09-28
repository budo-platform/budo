#include "desktop_host.h"

int desktop_host_run(Window *window, InputState *input,
                     const DesktopHostApplication *application)
{
    if (!window || !input || !application || !application->get_driver)
        return 1;

    while (window_poll_events(window, input))
    {
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