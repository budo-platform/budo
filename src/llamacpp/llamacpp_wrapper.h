#ifndef BUDO_LLAMACPP_WRAPPER_H
#define BUDO_LLAMACPP_WRAPPER_H

#include "file/file_wrapper.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct BudoLlamaModel BudoLlamaModel;
    typedef struct BudoLlamaChat BudoLlamaChat;

    typedef struct
    {
        uint32_t context_size;
        int32_t gpu_layers;
        int32_t threads;
        int32_t batch_size;
        bool use_mmap;
        bool allow_fallback;
        char device[64];
    } BudoLlamaLoadOptions;

    typedef struct
    {
        uint32_t max_tokens;
        uint32_t min_tokens;
        float temperature;
        int32_t top_k;
        float top_p;
        float min_p;
        float repetition_penalty;
        uint32_t seed;
        bool random_seed;
        uint32_t stop_count;
        char stops[8][128];
    } BudoLlamaGenerateOptions;

    typedef struct
    {
        char name[128];
        char architecture[64];
        char quantization[64];
        uint64_t parameter_count;
        uint64_t file_size;
        uint32_t model_context_size;
        uint32_t active_context_size;
        uint32_t vocabulary_size;
        char chat_template[256];
        char backend[32];
        char device[128];
        int32_t gpu_layers;
        int32_t total_layers;
        uint64_t estimated_memory_bytes;
        char fallback_reason[256];
    } BudoLlamaModelInfo;

    typedef struct
    {
        char id[64];
        char backend[32];
        char name[128];
        bool available;
        bool has_memory;
        uint64_t memory_bytes;
        char reason[128];
    } BudoLlamaDeviceInfo;

    typedef bool (*BudoLlamaTextCallback)(const char *text, size_t length, void *opaque);
    typedef bool (*BudoLlamaCancelCallback)(void *opaque);

    void budo_llama_backend_init(void);
    void budo_llama_backend_shutdown(void);
    bool budo_llama_is_available(void);
    size_t budo_llama_get_devices(BudoLlamaDeviceInfo *devices, size_t capacity);
    BudoLlamaLoadOptions budo_llama_default_load_options(void);
    BudoLlamaGenerateOptions budo_llama_default_generate_options(void);
    BudoLlamaModel *budo_llama_model_load(FileNativeReference *reference,
                                          const BudoLlamaLoadOptions *options,
                                          char *error, size_t error_size);
    void budo_llama_model_free(BudoLlamaModel *model);
    const BudoLlamaModelInfo *budo_llama_model_info(const BudoLlamaModel *model);
    BudoLlamaChat *budo_llama_chat_create(BudoLlamaModel *model, const char *system_prompt,
                                          uint32_t context_size, char *error, size_t error_size);
    void budo_llama_chat_free(BudoLlamaChat *chat);
    void budo_llama_chat_clear(BudoLlamaChat *chat);
    bool budo_llama_chat_generate(BudoLlamaChat *chat, const char *text,
                                  const BudoLlamaGenerateOptions *options,
                                  BudoLlamaCancelCallback cancelled, void *cancel_opaque,
                                  BudoLlamaTextCallback callback, void *opaque,
                                  uint32_t *prompt_tokens, uint32_t *generated_tokens,
                                  double *prompt_tokens_per_second,
                                  double *generated_tokens_per_second,
                                  bool *stopped, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif