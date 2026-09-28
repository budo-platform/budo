#include "core/subsystem_registry.h"

#include <stdio.h>

typedef struct
{
    int id;
    int *events;
    size_t *event_count;
} TestContext;

static void record_poll(void *opaque)
{
    TestContext *context = (TestContext *)opaque;
    context->events[(*context->event_count)++] = context->id;
}

static void record_cleanup(void *opaque)
{
    TestContext *context = (TestContext *)opaque;
    context->events[(*context->event_count)++] = -context->id;
}

static void record_pause(void *opaque)
{
    TestContext *context = (TestContext *)opaque;
    context->events[(*context->event_count)++] = 100 + context->id;
}

static void record_resume(void *opaque)
{
    TestContext *context = (TestContext *)opaque;
    context->events[(*context->event_count)++] = 200 + context->id;
}

static void record_context_lost(void *opaque)
{
    TestContext *context = (TestContext *)opaque;
    context->events[(*context->event_count)++] = 300 + context->id;
}

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "subsystem_registry_test: %s\n", message);
    return 1;
}

int main(void)
{
    SubsystemRegistry registry;
    TestContext contexts[SUBSYSTEM_REGISTRY_CAPACITY + 1];
    int events[SUBSYSTEM_REGISTRY_CAPACITY * 3] = {0};
    size_t event_count = 0;
    size_t i;

    subsystem_registry_init(&registry);
    contexts[0] = (TestContext){1, events, &event_count};
    const SubsystemOps lifecycle_ops = {
        .poll = record_poll,
        .pause = record_pause,
        .resume = record_resume,
        .context_lost = record_context_lost,
        .cleanup = record_cleanup,
    };
    if (check(subsystem_registry_register_ops(
                  &registry, "test", &contexts[0], &lifecycle_ops),
              "valid registration was rejected"))
        return 1;
    if (check(!subsystem_registry_register(
                  &registry, "duplicate", &contexts[0], NULL, record_cleanup),
              "duplicate context was accepted"))
        return 1;

    for (i = 1; i < SUBSYSTEM_REGISTRY_CAPACITY; i++)
    {
        contexts[i] = (TestContext){(int)i + 1, events, &event_count};
        if (check(subsystem_registry_register(
                      &registry, "test", &contexts[i],
                      i < 3 ? record_poll : NULL, record_cleanup),
                  "valid registration was rejected"))
            return 1;
    }

    if (check(subsystem_registry_count(&registry) ==
                  SUBSYSTEM_REGISTRY_CAPACITY,
              "registration count differs"))
        return 1;

    contexts[SUBSYSTEM_REGISTRY_CAPACITY] =
        (TestContext){99, events, &event_count};
    if (check(!subsystem_registry_register(
                  &registry, "overflow",
                  &contexts[SUBSYSTEM_REGISTRY_CAPACITY], NULL,
                  record_cleanup),
              "capacity overflow was accepted"))
        return 1;

    subsystem_registry_poll(&registry);
    if (check(event_count == 3 && events[0] == 1 && events[1] == 2 &&
                  events[2] == 3,
              "poll order differs"))
        return 1;

    subsystem_registry_pause(&registry);
    subsystem_registry_pause(&registry);
    subsystem_registry_poll(&registry);
    if (check(event_count == 4 && events[3] == 101,
              "pause was not idempotent, reverse ordered, or poll ran paused"))
        return 1;
    subsystem_registry_resume(&registry);
    subsystem_registry_resume(&registry);
    if (check(event_count == 5 && events[4] == 201,
              "resume was not idempotent or registration ordered"))
        return 1;
    subsystem_registry_context_lost(&registry);
    if (check(event_count == 6 && events[5] == 301,
              "context loss was not reverse ordered"))
        return 1;

    subsystem_registry_shutdown(&registry);
    if (check(event_count == SUBSYSTEM_REGISTRY_CAPACITY + 6,
              "cleanup count differs"))
        return 1;
    for (i = 0; i < SUBSYSTEM_REGISTRY_CAPACITY; i++)
    {
        if (check(events[i + 6] ==
                      -(int)(SUBSYSTEM_REGISTRY_CAPACITY - i),
                  "cleanup order differs"))
            return 1;
    }

    subsystem_registry_shutdown(&registry);
    subsystem_registry_poll(&registry);
    subsystem_registry_pause(&registry);
    subsystem_registry_resume(&registry);
    subsystem_registry_context_lost(&registry);
    if (check(event_count == SUBSYSTEM_REGISTRY_CAPACITY + 6,
              "shutdown was not idempotent or poll ran after shutdown"))
        return 1;
    if (check(!subsystem_registry_register(
                  &registry, "late", &contexts[SUBSYSTEM_REGISTRY_CAPACITY],
                  NULL, record_cleanup),
              "post-shutdown registration was accepted"))
        return 1;

    subsystem_registry_init(&registry);
    if (check(!subsystem_registry_register(
                  NULL, "test", &contexts[0], NULL, record_cleanup) &&
                  !subsystem_registry_register(
                      &registry, NULL, &contexts[0], NULL, record_cleanup) &&
                  !subsystem_registry_register(
                      &registry, "test", NULL, NULL, record_cleanup) &&
                  !subsystem_registry_register(
                      &registry, "stateless-api", &contexts[0], NULL, NULL) &&
                  !subsystem_registry_register(
                      &registry, "test", &contexts[0], NULL, NULL) &&
                  subsystem_registry_count(NULL) == 0,
              "invalid registration input was accepted"))
        return 1;

    puts("subsystem_registry_test: ok");
    return 0;
}