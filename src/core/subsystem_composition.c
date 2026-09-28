#include "subsystem_composition.h"

bool subsystem_descriptors_have_valid_order(
    const SubsystemDescriptor *descriptors, size_t descriptor_count,
    const char **failed_subsystem)
{
    bool saw_canvas = false;
    bool saw_udp = false;
    bool saw_rtp_midi = false;

    if (failed_subsystem)
        *failed_subsystem = NULL;
    for (size_t i = 0; i < descriptor_count; i++)
    {
        const SubsystemDescriptor *descriptor = &descriptors[i];
        bool valid = true;
        switch (descriptor->role)
        {
        case SUBSYSTEM_ROLE_CORE:
            valid = i == 0;
            break;
        case SUBSYSTEM_ROLE_CANVAS:
            valid = !saw_canvas && i <= 1;
            saw_canvas = true;
            break;
        case SUBSYSTEM_ROLE_UDP:
            valid = saw_canvas;
            saw_udp = true;
            break;
        case SUBSYSTEM_ROLE_RTP_MIDI:
            valid = saw_udp;
            saw_rtp_midi = true;
            break;
        case SUBSYSTEM_ROLE_MIDI:
            valid = saw_canvas && (!saw_udp || saw_rtp_midi);
            break;
        case SUBSYSTEM_ROLE_SERVICE:
            valid = saw_canvas;
            break;
        }
        if (!valid)
        {
            if (failed_subsystem)
                *failed_subsystem = descriptor->name;
            return false;
        }
    }
    return descriptor_count == 0 || saw_canvas;
}

bool subsystem_compose(SubsystemRegistry *registry, void *host,
                       const SubsystemDescriptor *descriptors,
                       size_t descriptor_count,
                       const char **failed_subsystem)
{
    size_t i;

    if (failed_subsystem)
        *failed_subsystem = NULL;
    if (!registry || !host || (!descriptors && descriptor_count > 0) ||
        registry->shutdown_called)
        return false;
    if (!subsystem_descriptors_have_valid_order(
            descriptors, descriptor_count, failed_subsystem))
        return false;

    for (i = 0; i < descriptor_count; i++)
    {
        const SubsystemDescriptor *descriptor = &descriptors[i];
        void *context;

        if (!descriptor->name || !descriptor->initialize ||
            !descriptor->ops.cleanup)
        {
            if (failed_subsystem)
                *failed_subsystem = descriptor->name;
            subsystem_registry_shutdown(registry);
            return false;
        }

        context = descriptor->initialize(host);
        if (!context)
        {
            if (descriptor->optional)
                continue;
            if (failed_subsystem)
                *failed_subsystem = descriptor->name;
            subsystem_registry_shutdown(registry);
            return false;
        }

        if (!subsystem_registry_register_ops(registry, descriptor->name,
                                             context, &descriptor->ops))
        {
            descriptor->ops.cleanup(context);
            if (failed_subsystem)
                *failed_subsystem = descriptor->name;
            subsystem_registry_shutdown(registry);
            return false;
        }
    }

    return true;
}