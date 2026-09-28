#include "magneto_wrapper.h"
#include <stdlib.h>
#include <string.h>

struct MagnetoContext
{
    bool active;
};

MagnetoContext *magneto_create(void)
{
    MagnetoContext *ctx = calloc(1, sizeof(MagnetoContext));
    return ctx;
}

void magneto_destroy(MagnetoContext *ctx)
{
    if (ctx)
    {
        free(ctx);
    }
}

bool magneto_is_available(MagnetoContext *ctx)
{
    (void)ctx;
    return false;
}

bool magneto_has_accelerometer(MagnetoContext *ctx)
{
    (void)ctx;
    return false;
}

bool magneto_has_compass(MagnetoContext *ctx)
{
    (void)ctx;
    return false;
}

bool magneto_start(MagnetoContext *ctx)
{
    (void)ctx;
    return false;
}

void magneto_stop(MagnetoContext *ctx)
{
    (void)ctx;
}

bool magneto_is_active(MagnetoContext *ctx)
{
    (void)ctx;
    return false;
}

bool magneto_get_accel(MagnetoContext *ctx, MagnetoAccelData *data)
{
    (void)ctx;
    if (data)
    {
        memset(data, 0, sizeof(*data));
    }
    return false;
}

bool magneto_get_compass(MagnetoContext *ctx, MagnetoCompassData *data)
{
    (void)ctx;
    if (data)
    {
        memset(data, 0, sizeof(*data));
    }
    return false;
}