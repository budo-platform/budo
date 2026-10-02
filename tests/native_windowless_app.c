#include <budo/budo.h>

static BudoStatus on_initialize(BudoHost *host, void **app_state)
{
    (void)app_state;
    budo_host_log(host, BUDO_LOG_INFO, "native window-less app ran");
    budo_host_request_exit(host, 4);
    return BUDO_STATUS_OK;
}

static const BudoApplication application = {
    .struct_size = sizeof(BudoApplication),
    .api_version = BUDO_NATIVE_API_VERSION,
    .sdk_version = BUDO_VERSION_STRING,
    .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID,
    .name = "native windowless test",
    .initialize = on_initialize,
};

const BudoApplication *budo_get_application(void) { return &application; }