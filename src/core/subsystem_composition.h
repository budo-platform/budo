#ifndef BUDO_SUBSYSTEM_COMPOSITION_H
#define BUDO_SUBSYSTEM_COMPOSITION_H

#include "subsystem_registry.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void *(*SubsystemInitializeCallback)(void *host);

    typedef enum SubsystemRole
    {
        SUBSYSTEM_ROLE_CORE,
        SUBSYSTEM_ROLE_CANVAS,
        SUBSYSTEM_ROLE_SERVICE,
        SUBSYSTEM_ROLE_UDP,
        SUBSYSTEM_ROLE_RTP_MIDI,
        SUBSYSTEM_ROLE_MIDI
    } SubsystemRole;

    typedef struct SubsystemDescriptor
    {
        const char *name;
        SubsystemRole role;
        SubsystemInitializeCallback initialize;
        SubsystemOps ops;
        bool optional;
    } SubsystemDescriptor;

    bool subsystem_compose(SubsystemRegistry *registry, void *host,
                           const SubsystemDescriptor *descriptors,
                           size_t descriptor_count,
                           const char **failed_subsystem);

    bool subsystem_descriptors_have_valid_order(
        const SubsystemDescriptor *descriptors, size_t descriptor_count,
        const char **failed_subsystem);

#ifdef __cplusplus
}
#endif

#endif