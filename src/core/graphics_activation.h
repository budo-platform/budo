#ifndef BUDO_GRAPHICS_ACTIVATION_H
#define BUDO_GRAPHICS_ACTIVATION_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*BudoGraphicsActivateFn)(void *opaque);

    typedef struct BudoGraphicsActivation
    {
        BudoGraphicsActivateFn activate; 
        void *opaque;
        bool requested;
    } BudoGraphicsActivation;

    static inline void budo_graphics_activation_set(BudoGraphicsActivation *activation,
                                                    BudoGraphicsActivateFn activate,
                                                    void *opaque)
    {
        if (!activation)
            return;
        activation->activate = activate;
        activation->opaque = opaque;
        activation->requested = false;
    }

    static inline void budo_graphics_activation_request(BudoGraphicsActivation *activation)
    {
        if (!activation || activation->requested)
            return;
        activation->requested = true;
        if (activation->activate)
            activation->activate(activation->opaque);
    }

#ifdef __cplusplus
}
#endif

#endif