#ifndef BUDO_NATIVE_HOST_INTERNAL_H
#define BUDO_NATIVE_HOST_INTERNAL_H

#include <stddef.h>

#include <budo/budo.h>
#include "core/input.h"

typedef struct SkiaCanvas SkiaCanvas;
typedef struct Window Window;

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*BudoNativeLogCallback)(void *context, BudoLogLevel level,
                                          const char *message);

    struct BudoInput
    {
        const InputState *state;
    };

    struct BudoCanvas
    {
        BudoHost *host;
        SkiaCanvas *implementation;
    };

    struct BudoHost
    {
        BudoBuildIdentity identity;
        struct BudoInput input;
        BudoError last_error;
        char error_message[512];
        BudoNativeLogCallback log_callback;
        void *log_context;
        struct BudoCanvas canvas;
        Window *graphics_window;
        uint64_t graphics_generation;
        uintptr_t callback_thread;
        unsigned int callback_depth;
        bool callback_allows_graphics;
        bool surface_active;
        bool exit_requested;
        int exit_code;
    };

    void budo_native_host_init(BudoHost *host, const InputState *input,
                               BudoNativeLogCallback log_callback,
                               void *log_context);
    void budo_native_host_set_input(BudoHost *host, const InputState *input);
    void budo_native_host_set_canvas(BudoHost *host, SkiaCanvas *canvas);
    void budo_native_host_set_graphics_window(BudoHost *host, Window *window);
    void budo_native_host_surface_created(BudoHost *host);
    void budo_native_host_context_lost(BudoHost *host);
    void budo_native_host_enter_callback(BudoHost *host,
                                         bool allows_graphics);
    void budo_native_host_leave_callback(BudoHost *host);
    bool budo_native_host_graphics_available(BudoHost *host);
    void budo_native_host_reset(BudoHost *host);
    
    bool budo_native_host_exit_requested(const BudoHost *host, int *exit_code);

#ifdef __cplusplus
}
#endif

#endif