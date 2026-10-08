#ifndef BUDO_ANIMATION_WAIT_H
#define BUDO_ANIMATION_WAIT_H

#include "core/input.h"

#include <stdbool.h>

typedef struct
{
    bool waiting;
    double timeout_ms; 
    double since_ms;   
    int width;
    int height;
} BudoAnimationWait;

static inline void budo_animation_wait_start(BudoAnimationWait *wait, double timeout_ms, int width, int height)
{
    wait->waiting = true;
    wait->timeout_ms = timeout_ms;
    wait->since_ms = -1.0;
    wait->width = width;
    wait->height = height;
}

static inline void budo_animation_wait_stop(BudoAnimationWait *wait)
{
    wait->waiting = false;
}

static inline bool budo_animation_wait_due(BudoAnimationWait *wait, const InputState *input,
                                           int width, int height, double now_ms)
{
    if (!wait || !wait->waiting)
        return true;
    if (wait->since_ms < 0.0)
        wait->since_ms = now_ms;
    bool due = (input && input->event_count > 0) || width != wait->width || height != wait->height ||
               (wait->timeout_ms > 0.0 && now_ms - wait->since_ms >= wait->timeout_ms);
    if (due)
        wait->waiting = false;
    return due;
}

static inline double budo_animation_wait_remaining(const BudoAnimationWait *wait, double now_ms)
{
    if (!wait || !wait->waiting)
        return -1.0;
    if (wait->timeout_ms <= 0.0 || wait->since_ms < 0.0)
        return wait->timeout_ms > 0.0 ? wait->timeout_ms : 1e9;
    double remaining = wait->timeout_ms - (now_ms - wait->since_ms);
    return remaining > 0.0 ? remaining : 0.0;
}

#endif