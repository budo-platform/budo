#include "core/subsystem_queue.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PRODUCER_COUNT 4
#define ITEMS_PER_PRODUCER 256
#define TOTAL_ITEMS (PRODUCER_COUNT * ITEMS_PER_PRODUCER)

static int check(bool condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "subsystem_queue_test: %s\n", message);
    return 1;
}

static int test_fifo_and_wraparound(void)
{
    SubsystemQueue queue;
    int storage[3];
    int value;

    if (check(subsystem_queue_init(&queue, storage, sizeof(storage[0]), 3),
              "initialization failed"))
        return 1;
    if (check(subsystem_queue_try_push(&queue, &(int){10}) &&
                  subsystem_queue_try_push(&queue, &(int){20}) &&
                  subsystem_queue_try_push(&queue, &(int){30}),
              "initial pushes failed"))
        return 1;
    if (check(subsystem_queue_count(&queue) == 3,
              "initial count differs"))
        return 1;
    if (check(subsystem_queue_try_pop(&queue, &value) && value == 10 &&
                  subsystem_queue_try_pop(&queue, &value) && value == 20,
              "initial FIFO order differs"))
        return 1;
    if (check(subsystem_queue_try_push(&queue, &(int){40}) &&
                  subsystem_queue_try_push(&queue, &(int){50}),
              "wraparound pushes failed"))
        return 1;
    if (check(subsystem_queue_try_pop(&queue, &value) && value == 30 &&
                  subsystem_queue_try_pop(&queue, &value) && value == 40 &&
                  subsystem_queue_try_pop(&queue, &value) && value == 50,
              "wraparound FIFO order differs"))
        return 1;
    if (check(!subsystem_queue_try_pop(&queue, &value) &&
                  subsystem_queue_count(&queue) == 0 &&
                  subsystem_queue_dropped(&queue) == 0,
              "empty queue state differs"))
        return 1;

    subsystem_queue_destroy(&queue);
    return 0;
}

static int test_overflow(void)
{
    SubsystemQueue queue;
    int storage[2];
    int value;

    if (check(subsystem_queue_init(&queue, storage, sizeof(storage[0]), 2),
              "overflow queue initialization failed"))
        return 1;
    if (check(subsystem_queue_try_push(&queue, &(int){1}) &&
                  subsystem_queue_try_push(&queue, &(int){2}) &&
                  !subsystem_queue_try_push(&queue, &(int){3}) &&
                  !subsystem_queue_try_push(&queue, &(int){4}),
              "overflow behavior differs"))
        return 1;
    if (check(subsystem_queue_count(&queue) == 2 &&
                  subsystem_queue_dropped(&queue) == 2,
              "overflow counters differ"))
        return 1;
    if (check(subsystem_queue_try_pop(&queue, &value) && value == 1 &&
                  subsystem_queue_try_pop(&queue, &value) && value == 2,
              "overflow changed queued items"))
        return 1;

    subsystem_queue_destroy(&queue);
    return 0;
}

static int test_close_drain_and_reset(void)
{
    SubsystemQueue queue;
    int storage[2];
    int value;

    if (check(subsystem_queue_init(&queue, storage, sizeof(storage[0]), 2),
              "reset queue initialization failed"))
        return 1;
    if (check(!subsystem_queue_reset(&queue),
              "open queue was reset"))
        return 1;
    if (check(subsystem_queue_try_push(&queue, &(int){7}),
              "pre-close push failed"))
        return 1;
    subsystem_queue_close(&queue);
    if (check(subsystem_queue_is_closed(&queue) &&
                  !subsystem_queue_try_push(&queue, &(int){8}) &&
                  subsystem_queue_dropped(&queue) == 0,
              "close did not reject push cleanly"))
        return 1;
    if (check(!subsystem_queue_reset(&queue),
              "non-empty closed queue was reset"))
        return 1;
    if (check(subsystem_queue_try_pop(&queue, &value) && value == 7 &&
                  !subsystem_queue_try_pop(&queue, &value),
              "closed queue did not drain"))
        return 1;
    if (check(subsystem_queue_reset(&queue) &&
                  !subsystem_queue_is_closed(&queue) &&
                  subsystem_queue_count(&queue) == 0 &&
                  subsystem_queue_dropped(&queue) == 0 &&
                  subsystem_queue_try_push(&queue, &(int){9}) &&
                  subsystem_queue_try_pop(&queue, &value) && value == 9,
              "reset did not reopen an empty closed queue"))
        return 1;

    subsystem_queue_close(&queue);
    if (check(subsystem_queue_reset(&queue),
              "second reset failed"))
        return 1;
    subsystem_queue_destroy(&queue);
    return 0;
}

typedef struct
{
    SubsystemQueue *queue;
    int first_value;
    bool succeeded;
} ProducerContext;

static BUDO_THREAD_RETURN producer_main(void *opaque)
{
    ProducerContext *context = (ProducerContext *)opaque;
    int i;

    context->succeeded = true;
    for (i = 0; i < ITEMS_PER_PRODUCER; i++)
    {
        int value = context->first_value + i;
        if (!subsystem_queue_try_push(context->queue, &value))
        {
            context->succeeded = false;
            break;
        }
    }
    return BUDO_THREAD_RESULT;
}

static int test_multithreaded_producers(void)
{
    SubsystemQueue queue;
    int storage[TOTAL_ITEMS];
    bool seen[TOTAL_ITEMS];
    BudoThread threads[PRODUCER_COUNT];
    ProducerContext contexts[PRODUCER_COUNT];
    int i;

    memset(seen, 0, sizeof(seen));
    if (check(subsystem_queue_init(&queue, storage, sizeof(storage[0]),
                                   TOTAL_ITEMS),
              "threaded queue initialization failed"))
        return 1;

    for (i = 0; i < PRODUCER_COUNT; i++)
    {
        contexts[i].queue = &queue;
        contexts[i].first_value = i * ITEMS_PER_PRODUCER;
        contexts[i].succeeded = false;
        if (check(budo_thread_create(&threads[i], producer_main, &contexts[i]),
                  "producer thread creation failed"))
            return 1;
    }
    for (i = 0; i < PRODUCER_COUNT; i++)
        budo_thread_join(threads[i]);
    for (i = 0; i < PRODUCER_COUNT; i++)
    {
        if (check(contexts[i].succeeded, "producer push failed"))
            return 1;
    }
    if (check(subsystem_queue_count(&queue) == TOTAL_ITEMS &&
                  subsystem_queue_dropped(&queue) == 0,
              "threaded producer counters differ"))
        return 1;

    for (i = 0; i < TOTAL_ITEMS; i++)
    {
        int value;
        if (check(subsystem_queue_try_pop(&queue, &value) && value >= 0 &&
                      value < TOTAL_ITEMS && !seen[value],
                  "threaded producer item differs"))
            return 1;
        seen[value] = true;
    }
    if (check(subsystem_queue_count(&queue) == 0,
              "threaded queue did not drain"))
        return 1;

    subsystem_queue_destroy(&queue);
    return 0;
}

int main(void)
{
    if (test_fifo_and_wraparound() || test_overflow() ||
        test_close_drain_and_reset() || test_multithreaded_producers())
        return 1;

    puts("subsystem_queue_test: ok");
    return 0;
}