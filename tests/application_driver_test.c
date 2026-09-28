#include "core/application_driver.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct TestContext
{
    char events[128];
    size_t length;
    bool initialize_result;
} TestContext;

static void record(TestContext *context, char event)
{
    assert(context->length + 1 < sizeof(context->events));
    context->events[context->length++] = event;
    context->events[context->length] = '\0';
}

static bool on_initialize(void *opaque)
{
    TestContext *context = opaque;
    record(context, 'I');
    return context->initialize_result;
}

static void on_surface(void *opaque) { record(opaque, 'S'); }
static void on_resize(void *opaque, int width, int height, float density)
{
    assert(width == 800);
    assert(height == 600);
    assert(density == 2.0f);
    record(opaque, 'R');
}
static void on_frame(void *opaque, double timestamp_ms, double delta_seconds)
{
    assert(timestamp_ms == 1000.0);
    assert(delta_seconds == 0.016);
    record(opaque, 'F');
}
static void on_pause(void *opaque) { record(opaque, 'P'); }
static void on_resume(void *opaque) { record(opaque, 'U'); }
static void on_context_lost(void *opaque) { record(opaque, 'L'); }
static void on_shutdown(void *opaque) { record(opaque, 'X'); }
static void on_destroy(void *opaque) { record(opaque, 'D'); }

static const ApplicationDriverOps test_ops = {
    .initialize = on_initialize,
    .surface_created = on_surface,
    .resize = on_resize,
    .frame = on_frame,
    .pause = on_pause,
    .resume = on_resume,
    .context_lost = on_context_lost,
    .shutdown = on_shutdown,
    .destroy = on_destroy,
};

static void test_normal_lifecycle(void)
{
    ApplicationDriver driver;
    TestContext context = {.initialize_result = true};

    application_driver_init(&driver, &test_ops, &context);
    assert(application_driver_initialize(&driver));
    application_driver_surface_created(&driver);
    application_driver_frame(&driver, 1000.0, 0.016);
    application_driver_resize(&driver, 800, 600, 2.0f);
    application_driver_frame(&driver, 1000.0, 0.016);
    application_driver_pause(&driver);
    application_driver_pause(&driver);
    application_driver_frame(&driver, 1000.0, 0.016);
    application_driver_resume(&driver);
    application_driver_resume(&driver);
    application_driver_context_lost(&driver);
    application_driver_context_lost(&driver);
    application_driver_frame(&driver, 1000.0, 0.016);
    application_driver_surface_created(&driver);
    application_driver_frame(&driver, 1000.0, 0.016);
    application_driver_resize(&driver, 800, 600, 2.0f);
    application_driver_destroy(&driver);
    application_driver_destroy(&driver);

    assert(strcmp(context.events, "ISRFPULSRXD") == 0);
}

static void test_failed_initialization_cleanup(void)
{
    ApplicationDriver driver;
    TestContext context = {.initialize_result = false};

    application_driver_init(&driver, &test_ops, &context);
    assert(!application_driver_initialize(&driver));
    application_driver_surface_created(&driver);
    application_driver_frame(&driver, 1000.0, 0.016);
    application_driver_destroy(&driver);

    assert(strcmp(context.events, "IXD") == 0);
}

int main(void)
{
    test_normal_lifecycle();
    test_failed_initialization_cleanup();
    puts("application_driver_test: ok");
    return 0;
}