#include "midi/midi_event_queue.h"

#include "core/platform_thread.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    int handle;
    bool is_sysex; 
    MidiMessage message;
} QueuedEvent;

typedef struct
{
    size_t length;
    uint8_t data[MIDI_SYSEX_MAX_SIZE];
} QueuedSysEx;

struct MidiEventQueue
{
    BudoMutex mutex;
    QueuedEvent *events;
    size_t event_capacity, event_head, event_count;
    QueuedSysEx *sysex;
    size_t sysex_capacity, sysex_head, sysex_count;
    size_t dropped_messages, dropped_sysex;
};

MidiEventQueue *midi_event_queue_create(size_t message_capacity, size_t sysex_capacity)
{
    if (message_capacity == 0 || sysex_capacity == 0)
        return NULL;
    MidiEventQueue *queue = (MidiEventQueue *)calloc(1, sizeof(MidiEventQueue));
    if (!queue)
        return NULL;
    queue->events = (QueuedEvent *)calloc(message_capacity, sizeof(QueuedEvent));
    queue->sysex = (QueuedSysEx *)calloc(sysex_capacity, sizeof(QueuedSysEx));
    if (!queue->events || !queue->sysex || !budo_mutex_init(&queue->mutex))
    {
        free(queue->events);
        free(queue->sysex);
        free(queue);
        return NULL;
    }
    queue->event_capacity = message_capacity;
    queue->sysex_capacity = sysex_capacity;
    return queue;
}

void midi_event_queue_destroy(MidiEventQueue *queue)
{
    if (!queue)
        return;
    budo_mutex_destroy(&queue->mutex);
    free(queue->events);
    free(queue->sysex);
    free(queue);
}

static void append_event(MidiEventQueue *queue, int handle, bool is_sysex, const MidiMessage *message)
{
    QueuedEvent *slot = &queue->events[(queue->event_head + queue->event_count) % queue->event_capacity];
    slot->handle = handle;
    slot->is_sysex = is_sysex;
    slot->message = *message;
    queue->event_count++;
}

bool midi_event_queue_push_message(MidiEventQueue *queue, int handle, const MidiMessage *message)
{
    if (!queue || !message)
        return false;
    budo_mutex_lock(&queue->mutex);
    bool room = queue->event_count < queue->event_capacity;
    if (room)
        append_event(queue, handle, false, message);
    else
        queue->dropped_messages++;
    budo_mutex_unlock(&queue->mutex);
    return room;
}

bool midi_event_queue_push_sysex(MidiEventQueue *queue, int handle, const uint8_t *data, size_t length,
                                 uint64_t timestamp_us)
{
    if (!queue || !data || length == 0 || length > MIDI_SYSEX_MAX_SIZE)
        return false;
    budo_mutex_lock(&queue->mutex);

    bool room = queue->event_count < queue->event_capacity && queue->sysex_count < queue->sysex_capacity;
    if (room)
    {
        QueuedSysEx *slot = &queue->sysex[(queue->sysex_head + queue->sysex_count) % queue->sysex_capacity];
        memcpy(slot->data, data, length);
        slot->length = length;
        queue->sysex_count++;
        MidiMessage marker;
        memset(&marker, 0, sizeof(marker));
        marker.status = 0xF0;
        marker.timestamp = timestamp_us;
        append_event(queue, handle, true, &marker);
    }
    else
    {
        queue->dropped_sysex++;
    }
    budo_mutex_unlock(&queue->mutex);
    return room;
}

bool midi_event_queue_pop(MidiEventQueue *queue, MidiEvent *event, uint8_t *sysex_buffer)
{
    if (!queue || !event)
        return false;
    budo_mutex_lock(&queue->mutex);
    bool found = queue->event_count > 0;
    if (found)
    {
        QueuedEvent *slot = &queue->events[queue->event_head];
        queue->event_head = (queue->event_head + 1) % queue->event_capacity;
        queue->event_count--;
        event->handle = slot->handle;
        event->is_sysex = slot->is_sysex;
        event->message = slot->message;
        event->sysex_length = 0;
        if (slot->is_sysex && queue->sysex_count > 0)
        {
            QueuedSysEx *payload = &queue->sysex[queue->sysex_head];
            queue->sysex_head = (queue->sysex_head + 1) % queue->sysex_capacity;
            queue->sysex_count--;
            if (sysex_buffer)
            {
                memcpy(sysex_buffer, payload->data, payload->length);
                event->sysex_length = payload->length;
            }
        }
    }
    budo_mutex_unlock(&queue->mutex);
    return found;
}

size_t midi_event_queue_dropped_messages(MidiEventQueue *queue)
{
    if (!queue)
        return 0;
    budo_mutex_lock(&queue->mutex);
    size_t dropped = queue->dropped_messages;
    budo_mutex_unlock(&queue->mutex);
    return dropped;
}

size_t midi_event_queue_dropped_sysex(MidiEventQueue *queue)
{
    if (!queue)
        return 0;
    budo_mutex_lock(&queue->mutex);
    size_t dropped = queue->dropped_sysex;
    budo_mutex_unlock(&queue->mutex);
    return dropped;
}