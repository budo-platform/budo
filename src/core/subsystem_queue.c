#include "core/subsystem_queue.h"

#include <stdint.h>
#include <string.h>

bool subsystem_queue_init(SubsystemQueue *queue, void *storage,
                          size_t item_size, size_t capacity)
{
    if (!queue || !storage || item_size == 0 || capacity == 0 ||
        capacity > SIZE_MAX / item_size)
        return false;

    memset(queue, 0, sizeof(*queue));
    queue->storage = (unsigned char *)storage;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->mutex_ready = budo_mutex_init(&queue->mutex);
    return queue->mutex_ready;
}

void subsystem_queue_destroy(SubsystemQueue *queue)
{
    if (!queue || !queue->mutex_ready)
        return;

    budo_mutex_destroy(&queue->mutex);
    queue->mutex_ready = false;
}

bool subsystem_queue_try_push(SubsystemQueue *queue, const void *item)
{
    size_t tail;

    if (!queue || !queue->mutex_ready || !item)
        return false;

    budo_mutex_lock(&queue->mutex);
    if (queue->closed)
    {
        budo_mutex_unlock(&queue->mutex);
        return false;
    }
    if (queue->count == queue->capacity)
    {
        queue->dropped++;
        budo_mutex_unlock(&queue->mutex);
        return false;
    }

    tail = (queue->head + queue->count) % queue->capacity;
    memcpy(queue->storage + tail * queue->item_size, item, queue->item_size);
    queue->count++;
    budo_mutex_unlock(&queue->mutex);
    return true;
}

bool subsystem_queue_try_pop(SubsystemQueue *queue, void *item)
{
    if (!queue || !queue->mutex_ready || !item)
        return false;

    budo_mutex_lock(&queue->mutex);
    if (queue->count == 0)
    {
        budo_mutex_unlock(&queue->mutex);
        return false;
    }

    memcpy(item, queue->storage + queue->head * queue->item_size,
           queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    budo_mutex_unlock(&queue->mutex);
    return true;
}

void subsystem_queue_close(SubsystemQueue *queue)
{
    if (!queue || !queue->mutex_ready)
        return;

    budo_mutex_lock(&queue->mutex);
    queue->closed = true;
    budo_mutex_unlock(&queue->mutex);
}

bool subsystem_queue_reset(SubsystemQueue *queue)
{
    bool reset = false;

    if (!queue || !queue->mutex_ready)
        return false;

    budo_mutex_lock(&queue->mutex);
    if (queue->closed && queue->count == 0)
    {
        queue->head = 0;
        queue->dropped = 0;
        queue->closed = false;
        reset = true;
    }
    budo_mutex_unlock(&queue->mutex);
    return reset;
}

size_t subsystem_queue_count(SubsystemQueue *queue)
{
    size_t count;

    if (!queue || !queue->mutex_ready)
        return 0;

    budo_mutex_lock(&queue->mutex);
    count = queue->count;
    budo_mutex_unlock(&queue->mutex);
    return count;
}

size_t subsystem_queue_dropped(SubsystemQueue *queue)
{
    size_t dropped;

    if (!queue || !queue->mutex_ready)
        return 0;

    budo_mutex_lock(&queue->mutex);
    dropped = queue->dropped;
    budo_mutex_unlock(&queue->mutex);
    return dropped;
}

bool subsystem_queue_is_closed(SubsystemQueue *queue)
{
    bool closed;

    if (!queue || !queue->mutex_ready)
        return true;

    budo_mutex_lock(&queue->mutex);
    closed = queue->closed;
    budo_mutex_unlock(&queue->mutex);
    return closed;
}