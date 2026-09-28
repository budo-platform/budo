#include <budo/budo.h>
#include "core/application_driver.h"
#include "core/input.h"
#include "native/native_application_driver.h"
#include "native/native_host_internal.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct TestState
{
    char events[64];
    size_t length;
    BudoStatus initialize_status;
    int frames;
} TestState;

static TestState test_state;

static void record(char event)
{
    assert(test_state.length + 1 < sizeof(test_state.events));
    test_state.events[test_state.length++] = event;
    test_state.events[test_state.length] = '\0';
}

static BudoStatus on_initialize(BudoHost *host, void **app_state)
{
    const BudoBuildIdentity *identity = budo_host_build_identity(host);
    assert(identity);
    assert(identity->api_version == BUDO_NATIVE_API_VERSION);
    *app_state = &test_state;
    record('I');
    return test_state.initialize_status;
}

static void on_surface(BudoHost *host, void *app_state)
{
    assert(host && app_state == &test_state);
    record('S');
}

static void on_resize(BudoHost *host, void *app_state,
                      const BudoSurfaceInfo *surface)
{
    (void)host;
    assert(app_state == &test_state);
    assert(surface->struct_size == sizeof(*surface));
    assert(surface->width == 800 && surface->height == 600);
    assert(surface->density == 2.0f);
    record('R');
}

static void on_frame(BudoHost *host, void *app_state,
                     const BudoFrameInfo *frame)
{
    BudoPointer pointer;
    BudoTouchPoint touch;
    BudoModifiers modifiers;
    (void)host;
    assert(app_state == &test_state);
    assert(frame->struct_size == sizeof(*frame));
    assert(frame->timestamp_ms == 1000.0);
    assert(frame->delta_seconds == 0.016);
    assert(frame->frame_index == (uint64_t)test_state.frames);
    assert(frame->surface.width == 800 && frame->surface.height == 600);
    assert(frame->surface.density == 2.0f);
    assert(budo_input_pointer(frame->input, &pointer));
    assert(pointer.x == 25 && pointer.y == 50);
    assert(budo_input_key_down(frame->input, BUDO_KEY_A));
    assert(budo_input_key_pressed(frame->input, BUDO_KEY_A));
    assert(budo_input_touch_count(frame->input) == 1);
    assert(budo_input_touch(frame->input, 0, &touch));
    assert(touch.id == 7 && touch.x == 25 && touch.y == 50);
    modifiers = budo_input_modifiers(frame->input);
    assert(modifiers.shift && modifiers.control && !modifiers.alt &&
           !modifiers.meta);
    assert(budo_input_focused(frame->input));
    ++test_state.frames;
    record('F');
}

static void on_pause(BudoHost *host, void *state)
{
    (void)host;
    assert(state == &test_state);
    record('P');
}
static void on_resume(BudoHost *host, void *state)
{
    (void)host;
    assert(state == &test_state);
    record('U');
}
static void on_lost(BudoHost *host, void *state)
{
    (void)host;
    assert(state == &test_state);
    record('L');
}
static void on_shutdown(BudoHost *host, void *state)
{
    (void)host;
    assert(state == &test_state);
    record('X');
}

static const BudoApplication valid_application = {
    .struct_size = sizeof(BudoApplication),
    .api_version = BUDO_NATIVE_API_VERSION,
    .sdk_version = BUDO_VERSION_STRING,
    .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID,
    .name = "Contract test",
    .initialize = on_initialize,
    .surface_created = on_surface,
    .resize = on_resize,
    .frame = on_frame,
    .pause = on_pause,
    .resume = on_resume,
    .context_lost = on_lost,
    .shutdown = on_shutdown,
};

static void init_host(BudoHost *host, InputState *input)
{
    input_init(input);
    input_set_mouse_position(input, 25, 50);
    input_set_key(input, BUDO_KEY_A, true);
    input_touch_start(input, 7, 25, 50);
    input_set_modifiers(input, true, true, false, false);
    budo_native_host_init(host, input, NULL, NULL);
}

static void test_lifecycle(void)
{
    ApplicationDriver driver;
    NativeApplicationDriver adapter;
    BudoHost host;
    InputState input;

    memset(&test_state, 0, sizeof(test_state));
    test_state.initialize_status = BUDO_STATUS_OK;
    init_host(&host, &input);
    assert(native_application_driver_init(&driver, &adapter,
                                          &valid_application, &host) ==
           BUDO_STATUS_OK);
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
    assert(strcmp(test_state.events, "ISRFPULSRX") == 0);
    assert(test_state.frames == 1);
}

static void test_failed_initialize_cleanup(void)
{
    ApplicationDriver driver;
    NativeApplicationDriver adapter;
    BudoHost host;
    InputState input;

    memset(&test_state, 0, sizeof(test_state));
    test_state.initialize_status = BUDO_STATUS_APPLICATION_ERROR;
    init_host(&host, &input);
    assert(native_application_driver_init(&driver, &adapter,
                                          &valid_application, &host) ==
           BUDO_STATUS_OK);
    assert(!application_driver_initialize(&driver));
    application_driver_destroy(&driver);
    assert(strcmp(test_state.events, "IX") == 0);
    assert(budo_host_last_error(&host)->status ==
           BUDO_STATUS_APPLICATION_ERROR);
}

static void test_descriptor_validation(void)
{
    ApplicationDriver driver;
    NativeApplicationDriver adapter;
    BudoHost host;
    InputState input;
    BudoApplication application;

    init_host(&host, &input);
    application = valid_application;
    application.surface_created = NULL;
    application.resize = NULL;
    application.pause = NULL;
    application.resume = NULL;
    application.context_lost = NULL;
    application.shutdown = NULL;
    assert(native_application_driver_init(&driver, &adapter, &application,
                                          &host) == BUDO_STATUS_OK);

    application = valid_application;
    application.struct_size = offsetof(BudoApplication, shutdown);
    assert(native_application_driver_init(&driver, &adapter, &application,
                                          &host) ==
           BUDO_STATUS_INCOMPATIBLE_API);
    application = valid_application;
    application.api_version++;
    assert(native_application_driver_init(&driver, &adapter, &application,
                                          &host) ==
           BUDO_STATUS_INCOMPATIBLE_API);
    application = valid_application;
    application.sdk_build_id = "different";
    assert(native_application_driver_init(&driver, &adapter, &application,
                                          &host) ==
           BUDO_STATUS_INCOMPATIBLE_API);
    application = valid_application;
    application.initialize = NULL;
    assert(native_application_driver_init(&driver, &adapter, &application,
                                          &host) ==
           BUDO_STATUS_INVALID_ARGUMENT);
    application = valid_application;
    application.frame = NULL;
    assert(native_application_driver_init(&driver, &adapter, &application,
                                          &host) ==
           BUDO_STATUS_INVALID_ARGUMENT);
}

static void test_input_transitions(void)
{
    BudoHost host;
    InputState input;
    BudoPointer pointer;
    BudoTouchPoint touch;
    const BudoInput *public_input;

    input_init(&input);
    budo_native_host_init(&host, &input, NULL, NULL);
    public_input = budo_host_input(&host);
    input_set_mouse_button(&input, INPUT_MOUSE_LEFT, true);
    input_set_mouse_button(&input, INPUT_MOUSE_MIDDLE, true);
    input_set_mouse_button(&input, INPUT_MOUSE_RIGHT, true);
    assert(budo_input_pointer(public_input, &pointer));
    assert(pointer.pressed[BUDO_POINTER_LEFT]);
    assert(pointer.pressed[BUDO_POINTER_MIDDLE]);
    assert(pointer.pressed[BUDO_POINTER_RIGHT]);

    input_touch_start(&input, 42, 10, 20);
    input_touch_end(&input, 42, 12, 24);
    assert(budo_input_touch_count(public_input) == 1);
    assert(budo_input_touch(public_input, 0, &touch));
    assert(touch.id == 42 && touch.released && !touch.pressed);
    input_begin_frame(&input);
    assert(budo_input_touch_count(public_input) == 0);

    input_set_key(&input, BUDO_KEY_LEFT, true);
    input_begin_frame(&input);
    input_set_key(&input, BUDO_KEY_LEFT, false);
    assert(budo_input_key_released(public_input, BUDO_KEY_LEFT));
    input_set_focus(&input, false);
    assert(!budo_input_focused(public_input));
    assert(!budo_input_key_down(public_input, BUDO_KEY_LEFT));
}

int main(void)
{
    test_lifecycle();
    test_failed_initialize_cleanup();
    test_descriptor_validation();
    test_input_transitions();
    puts("native_application_driver_test: ok");
    return 0;
}