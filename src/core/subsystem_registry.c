#include "subsystem_registry.h"

#include <string.h>

void subsystem_registry_init(SubsystemRegistry *registry)
{
    if (registry)
        memset(registry, 0, sizeof(*registry));
}

bool subsystem_registry_register(SubsystemRegistry *registry,
                                 const char *name,
                                 void *context,
                                 SubsystemPollCallback poll,
                                 SubsystemCleanupCallback cleanup)
{
    const SubsystemOps ops = {
        .poll = poll,
        .cleanup = cleanup,
    };
    return subsystem_registry_register_ops(registry, name, context, &ops);
}

bool subsystem_registry_register_ops(SubsystemRegistry *registry,
                                     const char *name,
                                     void *context,
                                     const SubsystemOps *ops)
{
    size_t i;

    if (!registry || !name || !context || !ops || !ops->cleanup ||
        registry->shutdown_called ||
        registry->count >= SUBSYSTEM_REGISTRY_CAPACITY)
        return false;

    for (i = 0; i < registry->count; i++)
    {
        if (registry->entries[i].context == context)
            return false;
    }

    registry->entries[registry->count++] = (SubsystemRegistryEntry){
        name, context, *ops};
    return true;
}

void subsystem_registry_poll(SubsystemRegistry *registry)
{
    size_t i;

    if (!registry || registry->shutdown_called || registry->paused)
        return;

    for (i = 0; i < registry->count; i++)
    {
        if (registry->entries[i].ops.poll)
            registry->entries[i].ops.poll(registry->entries[i].context);
    }
}

void subsystem_registry_pause(SubsystemRegistry *registry)
{
    size_t i;

    if (!registry || registry->shutdown_called || registry->paused)
        return;
    registry->paused = true;
    for (i = registry->count; i > 0; i--)
    {
        if (registry->entries[i - 1].ops.pause)
            registry->entries[i - 1].ops.pause(
                registry->entries[i - 1].context);
    }
}

void subsystem_registry_resume(SubsystemRegistry *registry)
{
    size_t i;

    if (!registry || registry->shutdown_called || !registry->paused)
        return;
    for (i = 0; i < registry->count; i++)
    {
        if (registry->entries[i].ops.resume)
            registry->entries[i].ops.resume(registry->entries[i].context);
    }
    registry->paused = false;
}

void subsystem_registry_context_lost(SubsystemRegistry *registry)
{
    size_t i;

    if (!registry || registry->shutdown_called)
        return;
    for (i = registry->count; i > 0; i--)
    {
        if (registry->entries[i - 1].ops.context_lost)
            registry->entries[i - 1].ops.context_lost(
                registry->entries[i - 1].context);
    }
}

void subsystem_registry_shutdown(SubsystemRegistry *registry)
{
    size_t i;

    if (!registry || registry->shutdown_called)
        return;

    registry->shutdown_called = true;
    for (i = registry->count; i > 0; i--)
        registry->entries[i - 1].ops.cleanup(
            registry->entries[i - 1].context);
}

size_t subsystem_registry_count(const SubsystemRegistry *registry)
{
    return registry ? registry->count : 0;
}