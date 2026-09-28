#include <budo/budo.h>

#include "counter.h"

#include <stdlib.h>

#ifndef NATIVE_FIXTURE
#error "Generated native project did not apply manifest definitions"
#endif

static BudoStatus fixture_initialize(BudoHost *host, void **app_state)
{
    int *counter = calloc(1, sizeof(*counter));
    if (!counter)
    {
        budo_host_set_error(host, BUDO_STATUS_OUT_OF_MEMORY, "fixture allocation failed");
        return BUDO_STATUS_OUT_OF_MEMORY;
    }
    *app_state = counter;
    return BUDO_STATUS_OK;
}

static void fixture_frame(BudoHost *host, void *app_state,
                          const BudoFrameInfo *frame)
{
    int *counter = app_state;
    (void)host;
    (void)frame;
    *counter = native_fixture_next(*counter);
}

static void fixture_shutdown(BudoHost *host, void *app_state)
{
    (void)host;
    free(app_state);
}

static const BudoApplication fixture_application = {
    .struct_size = sizeof(BudoApplication),
    .api_version = BUDO_NATIVE_API_VERSION,
    .sdk_version = BUDO_VERSION_STRING,
    .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID,
    .name = "Native Project Fixture",
    .initialize = fixture_initialize,
    .frame = fixture_frame,
    .shutdown = fixture_shutdown,
};

const BudoApplication *budo_get_application(void)
{
    return &fixture_application;
}