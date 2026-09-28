#ifndef BUDO_PUBLIC_APPLICATION_H
#define BUDO_PUBLIC_APPLICATION_H

#include <stdint.h>

#include <budo/core.h>
#include <budo/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct BudoSurfaceInfo
    {
        uint32_t struct_size;
        uint32_t width;
        uint32_t height;
        float density;
    } BudoSurfaceInfo;

    typedef struct BudoFrameInfo
    {
        uint32_t struct_size;
        double timestamp_ms;
        double delta_seconds;
        uint64_t frame_index;
        BudoSurfaceInfo surface;
        const BudoInput *input;
    } BudoFrameInfo;

    typedef BudoStatus (*BudoInitializeCallback)(BudoHost *host, void **app_state);
    typedef void (*BudoSurfaceCreatedCallback)(BudoHost *host, void *app_state);
    typedef void (*BudoResizeCallback)(BudoHost *host, void *app_state,
                                       const BudoSurfaceInfo *surface);
    typedef void (*BudoFrameCallback)(BudoHost *host, void *app_state,
                                      const BudoFrameInfo *frame);
    typedef void (*BudoPauseCallback)(BudoHost *host, void *app_state);
    typedef void (*BudoResumeCallback)(BudoHost *host, void *app_state);
    typedef void (*BudoContextLostCallback)(BudoHost *host, void *app_state);
    typedef void (*BudoShutdownCallback)(BudoHost *host, void *app_state);

    typedef struct BudoApplication
    {
        uint32_t struct_size;
        uint32_t api_version;
        const char *sdk_version;
        const char *sdk_build_id;
        const char *name;
        BudoInitializeCallback initialize;
        BudoSurfaceCreatedCallback surface_created;
        BudoResizeCallback resize;
        BudoFrameCallback frame;
        BudoPauseCallback pause;
        BudoResumeCallback resume;
        BudoContextLostCallback context_lost;
        BudoShutdownCallback shutdown;
    } BudoApplication;

    const BudoApplication *budo_get_application(void);

#ifdef __cplusplus
}
#endif

#endif