#include "core/subsystem_composition.h"

#include <stdio.h>
#include <string.h>

typedef struct TestHost
{
    int contexts[4];
    int initialized[4];
    int cleaned[4];
} TestHost;

static TestHost *active_host;

static void *initialize_first(void *opaque)
{
    TestHost *host = opaque;
    host->initialized[0]++;
    return &host->contexts[0];
}

static void *initialize_optional(void *opaque)
{
    TestHost *host = opaque;
    host->initialized[1]++;
    return NULL;
}

static void *initialize_second(void *opaque)
{
    TestHost *host = opaque;
    host->initialized[2]++;
    return &host->contexts[2];
}

static void *initialize_failure(void *opaque)
{
    TestHost *host = opaque;
    host->initialized[3]++;
    return NULL;
}

static void cleanup_context(void *opaque)
{
    int *context = opaque;
    size_t index = (size_t)(context - active_host->contexts);
    active_host->cleaned[index]++;
}

static int check(bool condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "subsystem_composition_test: %s\n", message);
    return 1;
}

int main(void)
{
    SubsystemRegistry registry;
    TestHost host = {0};
    const char *failed = NULL;
    const SubsystemOps ops = {.cleanup = cleanup_context};
    const SubsystemDescriptor success[] = {
        {"first", SUBSYSTEM_ROLE_CANVAS, initialize_first, ops, false},
        {"optional", SUBSYSTEM_ROLE_SERVICE, initialize_optional, ops, true},
        {"second", SUBSYSTEM_ROLE_SERVICE, initialize_second, ops, false},
    };
    const SubsystemDescriptor failure[] = {
        {"first", SUBSYSTEM_ROLE_CANVAS, initialize_first, ops, false},
        {"failure", SUBSYSTEM_ROLE_SERVICE, initialize_failure, ops, false},
    };
    const SubsystemDescriptor invalid_order[] = {
        {"first", SUBSYSTEM_ROLE_CANVAS, initialize_first, ops, false},
        {"udp", SUBSYSTEM_ROLE_UDP, initialize_second, ops, false},
        {"midi", SUBSYSTEM_ROLE_MIDI, initialize_second, ops, false},
    };

    active_host = &host;
    subsystem_registry_init(&registry);
    if (check(subsystem_compose(&registry, &host, success,
                                sizeof(success) / sizeof(success[0]), &failed),
              "valid composition failed") ||
        check(subsystem_registry_count(&registry) == 2,
              "optional subsystem was registered") ||
        check(host.initialized[0] == 1 && host.initialized[1] == 1 &&
                  host.initialized[2] == 1,
              "declaration order was not followed"))
        return 1;
    subsystem_registry_shutdown(&registry);
    if (check(host.cleaned[0] == 1 && host.cleaned[2] == 1,
              "composed contexts were not cleaned"))
        return 1;

    memset(&host, 0, sizeof(host));
    subsystem_registry_init(&registry);
    if (check(!subsystem_compose(&registry, &host, failure,
                                 sizeof(failure) / sizeof(failure[0]), &failed),
              "required initialization failure was accepted") ||
        check(failed && strcmp(failed, "failure") == 0,
              "failed subsystem was not reported") ||
        check(host.cleaned[0] == 1,
              "prior subsystem was not rolled back") ||
        check(registry.shutdown_called,
              "failed composition did not close the registry"))
        return 1;

    memset(&host, 0, sizeof(host));
    subsystem_registry_init(&registry);
    if (check(!subsystem_compose(
                  &registry, &host, invalid_order,
                  sizeof(invalid_order) / sizeof(invalid_order[0]), &failed),
              "invalid dependency order was accepted") ||
        check(failed && strcmp(failed, "midi") == 0,
              "invalid-order subsystem was not reported") ||
        check(host.initialized[0] == 0,
              "initialization began before order validation"))
        return 1;

    puts("subsystem_composition_test: ok");
    return 0;
}