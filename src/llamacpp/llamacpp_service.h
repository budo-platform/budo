#ifndef BUDO_LLAMACPP_SERVICE_H
#define BUDO_LLAMACPP_SERVICE_H

#include "llamacpp_wrapper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct LlamaCppService LlamaCppService;
    typedef uint64_t LlamaCppHandle;

    typedef enum
    {
        LLAMACPP_EVENT_MODEL_LOADED,
        LLAMACPP_EVENT_TEXT,
        LLAMACPP_EVENT_COMPLETE,
        LLAMACPP_EVENT_ERROR
    } LlamaCppEventType;

    typedef struct
    {
        LlamaCppEventType type;
        uint64_t request_id;
        LlamaCppHandle model;
        LlamaCppHandle chat;
        char *text;
        size_t text_length;
        BudoLlamaModelInfo model_info;
        uint64_t file_identity_high;
        uint64_t file_identity_low;
        uint64_t file_size;
        uint32_t context_size;
        int32_t batch_size;
        uint32_t prompt_tokens;
        uint32_t generated_tokens;
        double prompt_tokens_per_second;
        double generated_tokens_per_second;
        bool cancelled;
        bool stopped;
    } LlamaCppEvent;

    typedef void (*LlamaCppEventCallback)(const LlamaCppEvent *event, void *opaque);

    LlamaCppService *llamacpp_service_create(FileContext *files);
    void llamacpp_service_destroy(LlamaCppService *service);
    bool llamacpp_service_is_available(const LlamaCppService *service);
    size_t llamacpp_service_get_devices(LlamaCppService *service,
                                        BudoLlamaDeviceInfo *devices, size_t capacity);
    const char *llamacpp_service_get_error(const LlamaCppService *service);
    void llamacpp_service_set_event_callback(LlamaCppService *service,
                                             LlamaCppEventCallback callback, void *opaque);
    bool llamacpp_service_load_model(LlamaCppService *service, uint64_t request_id,
                                     const char *path, const BudoLlamaLoadOptions *options);
    LlamaCppHandle llamacpp_service_create_chat(LlamaCppService *service,
                                                LlamaCppHandle model,
                                                const char *system_prompt,
                                                uint32_t context_size);
    bool llamacpp_service_generate(LlamaCppService *service, uint64_t request_id,
                                   LlamaCppHandle chat, const char *text,
                                   const BudoLlamaGenerateOptions *options);
    void llamacpp_service_cancel(LlamaCppService *service, LlamaCppHandle chat);
    void llamacpp_service_cancel_all(LlamaCppService *service);
    void llamacpp_service_clear_chat(LlamaCppService *service, LlamaCppHandle chat);
    void llamacpp_service_destroy_chat(LlamaCppService *service, LlamaCppHandle chat);
    void llamacpp_service_destroy_model(LlamaCppService *service, LlamaCppHandle model);
    void llamacpp_service_poll(LlamaCppService *service);

#ifdef __cplusplus
}
#endif
#endif