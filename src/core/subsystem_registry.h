#ifndef BUDO_SUBSYSTEM_REGISTRY_H
#define BUDO_SUBSYSTEM_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SUBSYSTEM_REGISTRY_CAPACITY 32

    typedef void (*SubsystemPollCallback)(void *context);
    typedef void (*SubsystemLifecycleCallback)(void *context);
    typedef void (*SubsystemCleanupCallback)(void *context);
    typedef bool (*SubsystemPendingWorkCallback)(void *context);

    typedef struct SubsystemOps
    {
        SubsystemPollCallback poll;
        SubsystemLifecycleCallback pause;
        SubsystemLifecycleCallback resume;
        SubsystemLifecycleCallback context_lost;
        SubsystemCleanupCallback cleanup;

        SubsystemPendingWorkCallback has_pending_work;
    } SubsystemOps;

    typedef struct SubsystemRegistryEntry
    {
        const char *name;
        void *context;
        SubsystemOps ops;
    } SubsystemRegistryEntry;

    typedef struct SubsystemRegistry
    {
        SubsystemRegistryEntry entries[SUBSYSTEM_REGISTRY_CAPACITY];
        size_t count;
        bool paused;
        bool shutdown_called;
    } SubsystemRegistry;

    void subsystem_registry_init(SubsystemRegistry *registry);
    bool subsystem_registry_register(SubsystemRegistry *registry,
                                     const char *name,
                                     void *context,
                                     SubsystemPollCallback poll,
                                     SubsystemCleanupCallback cleanup);
    bool subsystem_registry_register_ops(SubsystemRegistry *registry,
                                         const char *name,
                                         void *context,
                                         const SubsystemOps *ops);
    void subsystem_registry_poll(SubsystemRegistry *registry);
    void subsystem_registry_pause(SubsystemRegistry *registry);
    void subsystem_registry_resume(SubsystemRegistry *registry);
    void subsystem_registry_context_lost(SubsystemRegistry *registry);
    void subsystem_registry_shutdown(SubsystemRegistry *registry);
    size_t subsystem_registry_count(const SubsystemRegistry *registry);
    bool subsystem_registry_has_pending_work(const SubsystemRegistry *registry);

#ifdef __cplusplus
}
#endif

#endif