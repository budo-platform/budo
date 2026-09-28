#include "llamacpp_wrapper.h"

#include <llama.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <io.h>
#endif

struct BudoLlamaModel
{
    llama_model *model = nullptr;
    BudoLlamaModelInfo info{};
    BudoLlamaLoadOptions options{};
};

struct BudoLlamaChat
{
    BudoLlamaModel *owner = nullptr;
    llama_context *context = nullptr;
    std::vector<llama_chat_message> messages;
    std::vector<std::string> roles;
    std::vector<std::string> contents;
    std::string system_prompt;
};

static std::mutex backend_mutex;
static size_t backend_users;

static void set_error(char *error, size_t size, const char *message)
{
    if (error && size)
        std::snprintf(error, size, "%s", message ? message : "llama.cpp operation failed");
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity)
        std::snprintf(destination, capacity, "%s", source ? source : "");
}

void budo_llama_backend_init(void)
{
    std::lock_guard<std::mutex> lock(backend_mutex);
    if (backend_users++ == 0)
        llama_backend_init();
}

void budo_llama_backend_shutdown(void)
{
    std::lock_guard<std::mutex> lock(backend_mutex);
    if (backend_users && --backend_users == 0)
        llama_backend_free();
}

bool budo_llama_is_available(void) { return true; }

size_t budo_llama_get_devices(BudoLlamaDeviceInfo *devices, size_t capacity)
{
    size_t count = ggml_backend_dev_count();
    if (!devices)
        return count;
    size_t written = std::min(count, capacity);
    for (size_t index = 0; index < written; ++index)
    {
        ggml_backend_dev_t device = ggml_backend_dev_get(index);
        BudoLlamaDeviceInfo &info = devices[index];
        std::memset(&info, 0, sizeof(info));
        const char *name = ggml_backend_dev_name(device);
        enum ggml_backend_dev_type type = ggml_backend_dev_type(device);
        const char *backend = type == GGML_BACKEND_DEVICE_TYPE_CPU ? "cpu"
                              : type == GGML_BACKEND_DEVICE_TYPE_GPU ||
                                      type == GGML_BACKEND_DEVICE_TYPE_IGPU
#ifdef __APPLE__
                                  ? "metal"
#else
                                  ? "gpu"
#endif
                                  : "accelerator";
        bool first_backend = true;
        for (size_t prior = 0; prior < index; ++prior)
        {
            enum ggml_backend_dev_type prior_type =
                ggml_backend_dev_type(ggml_backend_dev_get(prior));
            bool same_type = (type == GGML_BACKEND_DEVICE_TYPE_CPU &&
                              prior_type == GGML_BACKEND_DEVICE_TYPE_CPU) ||
                             (type != GGML_BACKEND_DEVICE_TYPE_CPU &&
                              prior_type != GGML_BACKEND_DEVICE_TYPE_CPU);
            if (same_type)
            {
                first_backend = false;
                break;
            }
        }
        std::snprintf(info.id, sizeof(info.id), first_backend ? "%s" : "%s:%zu",
                      backend, index);
        std::snprintf(info.backend, sizeof(info.backend), "%s", backend);
        std::snprintf(info.name, sizeof(info.name), "%s", name ? name : backend);
        size_t free_memory = 0, total_memory = 0;
        ggml_backend_dev_memory(device, &free_memory, &total_memory);
        info.available = true;
        info.has_memory = total_memory != 0;
        info.memory_bytes = total_memory;
    }
    return count;
}

BudoLlamaLoadOptions budo_llama_default_load_options(void)
{
    BudoLlamaLoadOptions options{};
    options.context_size = 2048;
    options.gpu_layers = -1;
    options.threads = std::max(1u, std::thread::hardware_concurrency());
    options.batch_size = 512;
    options.use_mmap = true;
    options.allow_fallback = true;
    copy_text(options.device, sizeof(options.device), "auto");
    return options;
}

BudoLlamaGenerateOptions budo_llama_default_generate_options(void)
{
    BudoLlamaGenerateOptions options{};
    options.max_tokens = 256;
    options.min_tokens = 0;
    options.temperature = 0.8f;
    options.top_k = 40;
    options.top_p = 0.95f;
    options.min_p = 0.05f;
    options.repetition_penalty = 1.0f;
    options.seed = LLAMA_DEFAULT_SEED;
    options.random_seed = true;
    return options;
}

BudoLlamaModel *budo_llama_model_load(FileNativeReference *reference,
                                      const BudoLlamaLoadOptions *options,
                                      char *error, size_t error_size)
{
    if (!reference || !options)
    {
        set_error(error, error_size, "Invalid model load request");
        return nullptr;
    }
#ifdef _WIN32
    int fd = _open_osfhandle((intptr_t)reference->handle, 0);
    FILE *file = fd >= 0 ? _fdopen(fd, "rb") : nullptr;
    reference->handle = nullptr;
#else
    FILE *file = fdopen(reference->fd, "rb");
    reference->fd = -1;
#endif
    if (!file)
    {
#ifdef _WIN32
        if (fd >= 0)
            _close(fd);
#endif
        set_error(error, error_size, "Cannot create a model file stream");
        return nullptr;
    }
    llama_model_params params = llama_model_default_params();
    params.n_gpu_layers = options->gpu_layers;
    params.split_mode = LLAMA_SPLIT_MODE_NONE;
    params.use_mmap = options->use_mmap && llama_supports_mmap();
    ggml_backend_dev_t selected_devices[2] = {nullptr, nullptr};
    if (std::strcmp(options->device, "auto") != 0 &&
        std::strcmp(options->device, "cpu") != 0)
    {
        BudoLlamaDeviceInfo devices[16];
        size_t count = std::min(budo_llama_get_devices(devices, 16), (size_t)16);
        for (size_t index = 0; index < count; ++index)
        {
            if (std::strcmp(devices[index].id, options->device) == 0)
            {
                selected_devices[0] = ggml_backend_dev_get(index);
                params.devices = selected_devices;
                break;
            }
        }
        if (!selected_devices[0])
        {
            std::fclose(file);
            set_error(error, error_size, "Requested llama.cpp device is unavailable");
            return nullptr;
        }
    }
    llama_model *raw = llama_model_load_from_file_ptr(file, params);
    std::fclose(file);
    if (!raw)
    {
        set_error(error, error_size, "llama.cpp rejected the GGUF model");
        return nullptr;
    }
    auto *model = new BudoLlamaModel();
    model->model = raw;
    model->options = *options;
    auto &info = model->info;
    char description[128] = {};
    llama_model_desc(raw, description, sizeof(description));
    copy_text(info.name, sizeof(info.name), description);
    llama_model_meta_val_str(raw, "general.architecture", info.architecture, sizeof(info.architecture));
    copy_text(info.quantization, sizeof(info.quantization), llama_ftype_name(llama_model_ftype(raw)));
    info.parameter_count = llama_model_n_params(raw);
    info.file_size = reference->size;
    info.model_context_size = (uint32_t)std::max(0, llama_model_n_ctx_train(raw));
    info.active_context_size = options->context_size;
    info.vocabulary_size = (uint32_t)llama_vocab_n_tokens(llama_model_get_vocab(raw));
    copy_text(info.chat_template, sizeof(info.chat_template), llama_model_chat_template(raw, nullptr));
    copy_text(info.backend, sizeof(info.backend), options->gpu_layers == 0 ? "cpu" :
#ifdef __APPLE__
                                                                           "metal"
#else
                                                  llama_supports_gpu_offload() ? "gpu"
                                                                               : "cpu"
#endif
    );
    copy_text(info.device, sizeof(info.device), info.backend);
    info.gpu_layers = options->gpu_layers < 0 ? llama_model_n_layer(raw) : options->gpu_layers;
    info.total_layers = llama_model_n_layer(raw);
    info.estimated_memory_bytes = llama_model_size(raw);
    if (!llama_supports_gpu_offload() && options->gpu_layers != 0)
    {
        info.gpu_layers = 0;
        copy_text(info.backend, sizeof(info.backend), "cpu");
        copy_text(info.device, sizeof(info.device), "cpu");
        copy_text(info.fallback_reason, sizeof(info.fallback_reason), "No accelerated backend is available");
    }
    return model;
}

void budo_llama_model_free(BudoLlamaModel *model)
{
    if (!model)
        return;
    llama_model_free(model->model);
    delete model;
}

const BudoLlamaModelInfo *budo_llama_model_info(const BudoLlamaModel *model)
{
    return model ? &model->info : nullptr;
}

BudoLlamaChat *budo_llama_chat_create(BudoLlamaModel *model, const char *system_prompt,
                                      uint32_t context_size, char *error, size_t error_size)
{
    if (!model)
    {
        set_error(error, error_size, "Model is not available");
        return nullptr;
    }
    llama_context_params params = llama_context_default_params();
    params.n_ctx = context_size ? context_size : model->info.active_context_size;
    params.n_batch = std::min(params.n_ctx, (uint32_t)std::max(1, model->options.batch_size));
    params.n_threads = std::max(1, model->options.threads);
    params.n_threads_batch = params.n_threads;
    params.no_perf = false;
    auto *chat = new BudoLlamaChat();
    chat->owner = model;
    chat->context = llama_init_from_model(model->model, params);
    if (!chat->context)
    {
        delete chat;
        set_error(error, error_size, "Cannot allocate llama.cpp context");
        return nullptr;
    }
    chat->system_prompt = system_prompt ? system_prompt : "";
    return chat;
}

void budo_llama_chat_free(BudoLlamaChat *chat)
{
    if (!chat)
        return;
    llama_free(chat->context);
    delete chat;
}

void budo_llama_chat_clear(BudoLlamaChat *chat)
{
    if (!chat)
        return;
    llama_memory_clear(llama_get_memory(chat->context), true);
    chat->roles.clear();
    chat->contents.clear();
    chat->messages.clear();
}

bool budo_llama_chat_generate(BudoLlamaChat *chat, const char *text,
                              const BudoLlamaGenerateOptions *options,
                              BudoLlamaCancelCallback cancelled, void *cancel_opaque,
                              BudoLlamaTextCallback callback, void *opaque,
                              uint32_t *prompt_tokens, uint32_t *generated_tokens,
                              double *prompt_tokens_per_second,
                              double *generated_tokens_per_second,
                              bool *stopped, char *error, size_t error_size)
{
    if (!chat || !text || !options || !callback)
    {
        set_error(error, error_size, "Invalid generation request");
        return false;
    }
    std::vector<llama_chat_message> messages;
    if (!chat->system_prompt.empty())
        messages.push_back({"system", chat->system_prompt.c_str()});
    for (size_t i = 0; i < chat->roles.size(); ++i)
        messages.push_back({chat->roles[i].c_str(), chat->contents[i].c_str()});
    messages.push_back({"user", text});
    const char *tmpl = llama_model_chat_template(chat->owner->model, nullptr);
    int32_t prompt_size = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, nullptr, 0);
    if (prompt_size <= 0 || prompt_size > 4 * 1024 * 1024)
    {
        set_error(error, error_size, "Unsupported or oversized chat template output");
        return false;
    }
    std::string prompt((size_t)prompt_size, '\0');
    llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, prompt.data(), prompt_size);
    const llama_vocab *vocab = llama_model_get_vocab(chat->owner->model);
    int32_t token_count = llama_tokenize(vocab, prompt.data(), prompt_size, nullptr, 0, true, true);
    if (token_count >= 0)
    {
        set_error(error, error_size, "Tokenizer did not report required capacity");
        return false;
    }
    token_count = -token_count;
    const uint32_t context_size = llama_n_ctx(chat->context);
    if ((uint32_t)token_count >= context_size)
    {
        set_error(error, error_size, "Prompt exceeds the active context size");
        return false;
    }
    const uint32_t max_tokens = std::min(options->max_tokens,
                                         context_size - (uint32_t)token_count);
    const uint32_t min_tokens = std::min(options->min_tokens, max_tokens);
    std::vector<llama_token> tokens((size_t)token_count);
    llama_memory_clear(llama_get_memory(chat->context), true);
    if (llama_tokenize(vocab, prompt.data(), prompt_size, tokens.data(), token_count, true, true) < 0 ||
        llama_decode(chat->context, llama_batch_get_one(tokens.data(), token_count)) != 0)
    {
        set_error(error, error_size, "Prompt evaluation failed");
        return false;
    }
    llama_sampler *sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_penalties(64, options->repetition_penalty, 0, 0));
    int eog_bias_index = -1;
    if (min_tokens > 0)
    {
        std::vector<llama_logit_bias> eog_biases;
        int32_t vocabulary_size = llama_vocab_n_tokens(vocab);
        for (llama_token token = 0; token < vocabulary_size; ++token)
        {
            if (llama_vocab_is_eog(vocab, token))
                eog_biases.push_back({token, -INFINITY});
        }
        if (!eog_biases.empty())
        {
            eog_bias_index = llama_sampler_chain_n(sampler);
            llama_sampler_chain_add(
                sampler, llama_sampler_init_logit_bias(
                             vocabulary_size, (int32_t)eog_biases.size(),
                             eog_biases.data()));
        }
    }
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(options->top_k));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(options->top_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_min_p(options->min_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(options->temperature));
    llama_sampler_chain_add(sampler, options->temperature <= 0 ? llama_sampler_init_greedy() : llama_sampler_init_dist(options->seed));
    std::string answer;
    std::string pending_text;
    size_t maximum_stop_length = 0;
    for (uint32_t index = 0; index < options->stop_count; ++index)
        maximum_stop_length = std::max(maximum_stop_length,
                                       std::strlen(options->stops[index]));
    uint32_t produced = 0;
    bool reached_stop = false;
    bool stream_failed = false;
    while (produced < max_tokens && !(cancelled && cancelled(cancel_opaque)))
    {
        if (eog_bias_index >= 0 && produced >= min_tokens)
        {
            llama_sampler *removed = llama_sampler_chain_remove(
                sampler, eog_bias_index);
            llama_sampler_free(removed);
            eog_bias_index = -1;
        }
        llama_token token = llama_sampler_sample(sampler, chat->context, -1);
        if (llama_vocab_is_eog(vocab, token))
        {
            reached_stop = true;
            break;
        }
        char inline_piece[256];
        int32_t length = llama_token_to_piece(vocab, token, inline_piece,
                                              sizeof(inline_piece), 0, false);
        std::vector<char> extended_piece;
        const char *piece = inline_piece;
        if (length < 0)
        {
            if (length == INT32_MIN)
            {
                llama_sampler_free(sampler);
                set_error(error, error_size, "Token text exceeds supported limits");
                return false;
            }
            extended_piece.resize((size_t)-length);
            length = llama_token_to_piece(vocab, token, extended_piece.data(),
                                          (int32_t)extended_piece.size(), 0, false);
            piece = extended_piece.data();
        }
        if (length < 0)
            break;
        pending_text.append(piece, (size_t)length);
        size_t stop_position = std::string::npos;
        for (uint32_t index = 0; index < options->stop_count; ++index)
        {
            size_t position = pending_text.find(options->stops[index]);
            if (position < stop_position)
                stop_position = position;
        }
        if (stop_position != std::string::npos)
        {
            if (stop_position && !callback(pending_text.data(), stop_position, opaque))
            {
                stream_failed = true;
                break;
            }
            answer.append(pending_text.data(), stop_position);
            reached_stop = true;
            pending_text.clear();
            break;
        }
        size_t retain = maximum_stop_length ? maximum_stop_length - 1 : 0;
        if (pending_text.size() > retain)
        {
            size_t emit = pending_text.size() - retain;
            if (!callback(pending_text.data(), emit, opaque))
            {
                stream_failed = true;
                break;
            }
            answer.append(pending_text.data(), emit);
            pending_text.erase(0, emit);
        }
        produced++;
        if (llama_decode(chat->context, llama_batch_get_one(&token, 1)) != 0)
        {
            llama_sampler_free(sampler);
            set_error(error, error_size, "Token decode failed");
            return false;
        }
    }
    llama_sampler_free(sampler);
    if (!stream_failed && !pending_text.empty() &&
        callback(pending_text.data(), pending_text.size(), opaque))
        answer.append(pending_text);
    if (!(cancelled && cancelled(cancel_opaque)))
    {
        chat->roles.emplace_back("user");
        chat->contents.emplace_back(text);
        chat->roles.emplace_back("assistant");
        chat->contents.emplace_back(answer);
    }
    if (prompt_tokens)
        *prompt_tokens = (uint32_t)token_count;
    if (generated_tokens)
        *generated_tokens = produced;
    llama_perf_context_data perf = llama_perf_context(chat->context);
    if (prompt_tokens_per_second)
        *prompt_tokens_per_second = perf.t_p_eval_ms > 0
                                        ? 1000.0 * perf.n_p_eval / perf.t_p_eval_ms
                                        : 0.0;
    if (generated_tokens_per_second)
        *generated_tokens_per_second = perf.t_eval_ms > 0
                                           ? 1000.0 * perf.n_eval / perf.t_eval_ms
                                           : 0.0;
    llama_perf_context_reset(chat->context);
    if (stopped)
        *stopped = reached_stop;
    return true;
}
