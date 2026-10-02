#ifndef BUDO_MANAGED_SUBSYSTEMS_H
#define BUDO_MANAGED_SUBSYSTEMS_H

#include <stdbool.h>

#include "core/app_metadata.h"
#include "core/managed_runtime.h"
#include "core/subsystem_registry.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum ManagedSubsystemFeature
    {
        MANAGED_FEATURE_NONE = 0,
        MANAGED_FEATURE_AUDIO,
        MANAGED_FEATURE_MIDI,
        MANAGED_FEATURE_MAGNETO
    } ManagedSubsystemFeature;

    typedef struct ManagedHostConfig
    {
        
        const char *project_dir;
        
        const char *sqlite_dir;
        
        const char *files_root;

        const AppMetadata *metadata;
        
        bool (*feature_available)(ManagedSubsystemFeature feature);
        
        void (*network_created)(NetworkContext *network);
    } ManagedHostConfig;

    bool managed_subsystems_compose(ManagedRuntimeKind kind,
                                    SubsystemRegistry *registry,
                                    ManagedRuntimeCommon *contexts,
                                    const ManagedHostConfig *config,
                                    const char **failed_subsystem);

#ifdef __cplusplus
}
#endif

#endif