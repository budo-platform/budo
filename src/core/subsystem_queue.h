#ifndef BUDO_SUBSYSTEM_QUEUE_H
#define BUDO_SUBSYSTEM_QUEUE_H

#include "core/platform_thread.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        unsigned char *storage;
        size_t item_size;
        size_t capacity;
        size_t head;
        size_t count;
        size_t dropped;
        bool closed;
        bool mutex_ready;
        BudoMutex mutex;
    } SubsystemQueue;

    bool subsystem_queue_init(SubsystemQueue *queue, void *storage,
                              size_t item_size, size_t capacity);
    void subsystem_queue_destroy(SubsystemQueue *queue);

    bool subsystem_queue_try_push(SubsystemQueue *queue, const void *item);
    bool subsystem_queue_try_pop(SubsystemQueue *queue, void *item);

    void subsystem_queue_close(SubsystemQueue *queue);
    bool subsystem_queue_reset(SubsystemQueue *queue);

    size_t subsystem_queue_count(SubsystemQueue *queue);
    size_t subsystem_queue_dropped(SubsystemQueue *queue);
    bool subsystem_queue_is_closed(SubsystemQueue *queue);

#ifdef __cplusplus
}
#endif

#endif