#ifndef MAGNETO_WRAPPER_H
#define MAGNETO_WRAPPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        float x; 
        float y; 
        float z; 
    } MagnetoAccelData;

    typedef struct
    {
        float x;       
        float y;       
        float z;       
        float heading; 
    } MagnetoCompassData;

    typedef struct MagnetoContext MagnetoContext;

    MagnetoContext *magneto_create(void);

    void magneto_destroy(MagnetoContext *ctx);

    bool magneto_is_available(MagnetoContext *ctx);

    bool magneto_has_accelerometer(MagnetoContext *ctx);

    bool magneto_has_compass(MagnetoContext *ctx);

    bool magneto_start(MagnetoContext *ctx);

    void magneto_stop(MagnetoContext *ctx);

    bool magneto_is_active(MagnetoContext *ctx);

    bool magneto_get_accel(MagnetoContext *ctx, MagnetoAccelData *data);

    bool magneto_get_compass(MagnetoContext *ctx, MagnetoCompassData *data);

#ifdef __cplusplus
}
#endif

#endif