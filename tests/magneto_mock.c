#include "tests/magneto_mock.h"

#include <stdlib.h>

struct MagnetoContext
{
    bool active;
    float value;
};

static unsigned int next_context_value;

MagnetoContext *magneto_create(void)
{
    MagnetoContext *ctx = (MagnetoContext *)calloc(1, sizeof(MagnetoContext));
    if (ctx)
        ctx->value = (float)++next_context_value;
    return ctx;
}

void magneto_destroy(MagnetoContext *ctx)
{
    free(ctx);
}

void magneto_mock_set_value(MagnetoContext *ctx, float value)
{
    if (ctx)
        ctx->value = value;
}

bool magneto_is_available(MagnetoContext *ctx)
{
    return ctx != NULL;
}

bool magneto_has_accelerometer(MagnetoContext *ctx)
{
    return ctx != NULL;
}

bool magneto_has_compass(MagnetoContext *ctx)
{
    return ctx != NULL;
}

bool magneto_start(MagnetoContext *ctx)
{
    if (!ctx)
        return false;
    ctx->active = true;
    return true;
}

void magneto_stop(MagnetoContext *ctx)
{
    if (ctx)
        ctx->active = false;
}

bool magneto_is_active(MagnetoContext *ctx)
{
    return ctx && ctx->active;
}

bool magneto_get_accel(MagnetoContext *ctx, MagnetoAccelData *data)
{
    if (!ctx || !ctx->active || !data)
        return false;
    data->x = ctx->value;
    data->y = ctx->value + 1.0f;
    data->z = ctx->value + 2.0f;
    return true;
}

bool magneto_get_compass(MagnetoContext *ctx, MagnetoCompassData *data)
{
    if (!ctx || !ctx->active || !data)
        return false;
    data->x = ctx->value + 3.0f;
    data->y = ctx->value + 4.0f;
    data->z = ctx->value + 5.0f;
    data->heading = ctx->value + 6.0f;
    return true;
}