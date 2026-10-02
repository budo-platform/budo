#include "llamacpp_service.h"

#include "core/platform_thread.h"
#include "core/subsystem_queue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr size_t kEventCapacity = 128;
    constexpr size_t kMaxModelBytes = 64ull * 1024 * 1024 * 1024;
    constexpr size_t kMaxPromptBytes = 1024 * 1024;
    constexpr uint32_t kMaxGeneratedTokens = 8192;

    enum class JobType
    {
        Load,
        CreateChat,
        Generate,
        ClearChat,
        DestroyChat,
        DestroyModel,
        Stop
    };
    struct Job
    {
        JobType type;
        uint64_t request_id{};
        LlamaCppHandle handle{};
        LlamaCppHandle model{};
        FileNativeReference reference{};
        FileNativeReference fallback_reference{};
        bool has_fallback_reference{};
        bool placement_cache_hit{};
        BudoLlamaLoadOptions load{};
        BudoLlamaGenerateOptions generate{};
        std::string text;
        uint32_t context_size{};
    };
    struct ChatEntry
    {
        BudoLlamaChat *chat{};
        LlamaCppHandle model{};
        std::atomic_bool cancelled{false};
        bool running{};
    };
}

struct LlamaCppService
{
    FileContext *files{};
    std::atomic_bool closing{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::unordered_map<LlamaCppHandle, BudoLlamaModel *> models;
    std::unordered_set<LlamaCppHandle> live_models;
    std::unordered_map<LlamaCppHandle, ChatEntry *> chats;
    std::unordered_set<LlamaCppHandle> active_chats;
    std::unordered_set<uint64_t> outstanding_requests;
    std::atomic<uint64_t> next_handle{1};
    BudoThread worker{};
    bool worker_ready{};
    bool backend_ready{};
    SubsystemQueue events{};
    LlamaCppEvent event_storage[kEventCapacity]{};
    std::mutex terminal_mutex;
    std::deque<LlamaCppEvent> terminal_events;
    LlamaCppEventCallback callback{};
    void *callback_opaque{};
    char error[512]{};
};

static constexpr const char *kPlacementCachePath =
    "files/.llamacpp-placement-cache";
#ifndef BUDO_LLAMACPP_REVISION
#define BUDO_LLAMACPP_REVISION "unknown"
#endif

struct StreamState
{
    LlamaCppService *service;
    Job *job;
    std::string pending;
    bool invalid_utf8{};
    bool queue_overflow{};
};

static void ensure_backend(LlamaCppService *service)
{
    std::lock_guard<std::mutex> lock(service->mutex);
    if (!service->backend_ready)
    {
        budo_llama_backend_init();
        service->backend_ready = true;
    }
}

static bool cached_device_available(const char *device)
{
    if (std::strcmp(device, "cpu") == 0)
        return true;
    BudoLlamaDeviceInfo devices[16];
    size_t count = std::min(budo_llama_get_devices(devices, 16), (size_t)16);
    for (size_t index = 0; index < count; ++index)
    {
        if (devices[index].available &&
            (std::strcmp(device, devices[index].id) == 0 ||
             std::strcmp(device, devices[index].backend) == 0))
            return true;
    }
    return false;
}

static bool apply_placement_cache(LlamaCppService *service,
                                  const FileNativeReference *reference,
                                  BudoLlamaLoadOptions *options)
{
    if (std::strcmp(options->device, "auto") != 0 || options->gpu_layers != -1)
        return false;
    if (!file_is_file(service->files, kPlacementCachePath))
        return false;
    size_t length = 0;
    char *text = file_read_text(service->files, kPlacementCachePath, &length);
    if (!text)
        return false;
    char revision[64] = {};
    char device[64] = {};
    unsigned long long identity_high = 0, identity_low = 0, file_size = 0;
    unsigned context_size = 0;
    int batch_size = 0, gpu_layers = 0;
    int fields = std::sscanf(text, "%63s %llx %llx %llu %u %d %63s %d",
                             revision, &identity_high, &identity_low, &file_size,
                             &context_size, &batch_size, device, &gpu_layers);
    std::free(text);
    if (fields != 8 || std::strcmp(revision, BUDO_LLAMACPP_REVISION) != 0 ||
        identity_high != reference->identity_high ||
        identity_low != reference->identity_low || file_size != reference->size ||
        context_size != options->context_size || batch_size != options->batch_size ||
        gpu_layers < 0 || !cached_device_available(device))
        return false;
    std::snprintf(options->device, sizeof(options->device), "%s", device);
    options->gpu_layers = gpu_layers;
    return true;
}

static void publish_placement_cache(LlamaCppService *service,
                                    const LlamaCppEvent *event)
{
    if (!service || !event || event->type != LLAMACPP_EVENT_MODEL_LOADED)
        return;
    char record[512];
    int length = std::snprintf(
        record, sizeof(record), "%s %llx %llx %llu %u %d %s %d\n",
        BUDO_LLAMACPP_REVISION,
        (unsigned long long)event->file_identity_high,
        (unsigned long long)event->file_identity_low,
        (unsigned long long)event->file_size, event->context_size,
        event->batch_size, event->model_info.device, event->model_info.gpu_layers);
    if (length > 0 && (size_t)length < sizeof(record))
        file_write_text(service->files, kPlacementCachePath, record, (size_t)length);
}

static void initialize_native_reference(FileNativeReference *reference)
{
    std::memset(reference, 0, sizeof(*reference));
#ifndef _WIN32
    reference->fd = -1;
#endif
}

static size_t valid_utf8_prefix(const std::string &text, bool *invalid)
{
    size_t index = 0;
    *invalid = false;
    while (index < text.size())
    {
        unsigned char first = (unsigned char)text[index];
        size_t width = first < 0x80 ? 1 : (first & 0xe0) == 0xc0 ? 2
                                      : (first & 0xf0) == 0xe0   ? 3
                                      : (first & 0xf8) == 0xf0   ? 4
                                                                 : 0;
        if (!width)
        {
            *invalid = true;
            return index;
        }
        if (index + width > text.size())
            return index;
        for (size_t continuation = 1; continuation < width; ++continuation)
        {
            if (((unsigned char)text[index + continuation] & 0xc0) != 0x80)
            {
                *invalid = true;
                return index;
            }
        }
        index += width;
    }
    return index;
}

static char *copy_payload(const char *text, size_t length)
{
    char *copy = static_cast<char *>(std::malloc(length + 1));
    if (!copy)
        return nullptr;
    std::memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void free_event(LlamaCppEvent &event)
{
    std::free(event.text);
    event.text = nullptr;
}

static void push_event(LlamaCppService *service, LlamaCppEvent event, bool terminal)
{
    if (service->closing)
    {
        free_event(event);
        return;
    }
    if (terminal)
    {
        std::lock_guard<std::mutex> lock(service->terminal_mutex);
        service->terminal_events.push_back(event);
    }
    else if (!subsystem_queue_try_push(&service->events, &event))
    {
        free_event(event);
    }
}

static void push_error(LlamaCppService *service, uint64_t request, LlamaCppHandle model,
                       LlamaCppHandle chat, const char *message)
{
    LlamaCppEvent event{};
    event.type = LLAMACPP_EVENT_ERROR;
    event.request_id = request;
    event.model = model;
    event.chat = chat;
    event.text_length = std::strlen(message);
    event.text = copy_payload(message, event.text_length);
    push_event(service, event, true);
}

static bool stream_text(const char *text, size_t length, void *opaque)
{
    auto *stream = static_cast<StreamState *>(opaque);
    stream->pending.append(text, length);
    bool invalid = false;
    size_t complete = valid_utf8_prefix(stream->pending, &invalid);
    if (invalid)
    {
        stream->invalid_utf8 = true;
        return false;
    }
    if (!complete)
        return true;
    LlamaCppEvent event{};
    event.type = LLAMACPP_EVENT_TEXT;
    event.request_id = stream->job->request_id;
    event.chat = stream->job->handle;
    event.text = copy_payload(stream->pending.data(), complete);
    event.text_length = event.text ? complete : 0;
    if (!event.text)
        return false;
    if (!subsystem_queue_try_push(&stream->service->events, &event))
    {
        free_event(event);
        stream->queue_overflow = true;
        return false;
    }
    stream->pending.erase(0, complete);
    return !stream->service->closing;
}

static bool generation_cancelled(void *opaque)
{
    return static_cast<ChatEntry *>(opaque)->cancelled.load();
}

static void run_job(LlamaCppService *service, Job &job)
{
    if (job.type == JobType::Load)
    {
        ensure_backend(service);
        job.placement_cache_hit = apply_placement_cache(
            service, &job.reference, &job.load);
        char error[512]{};
        uint64_t identity_high = job.reference.identity_high;
        uint64_t identity_low = job.reference.identity_low;
        uint64_t file_size = job.reference.size;
        BudoLlamaModel *model = budo_llama_model_load(&job.reference, &job.load, error, sizeof(error));
        file_native_close(&job.reference);
        bool used_fallback = false;
        if (!model && job.has_fallback_reference)
        {
            BudoLlamaLoadOptions cpu_options = job.load;
            cpu_options.gpu_layers = 0;
            std::snprintf(cpu_options.device, sizeof(cpu_options.device), "cpu");
            model = budo_llama_model_load(&job.fallback_reference, &cpu_options,
                                          error, sizeof(error));
            used_fallback = model != nullptr;
        }
        file_native_close(&job.fallback_reference);
        if (!model)
        {
            push_error(service, job.request_id, 0, 0, error);
            return;
        }
        LlamaCppHandle handle = service->next_handle++;
        service->models[handle] = model;
        {
            std::lock_guard<std::mutex> lock(service->mutex);
            service->live_models.insert(handle);
        }
        LlamaCppEvent event{};
        event.type = LLAMACPP_EVENT_MODEL_LOADED;
        event.request_id = job.request_id;
        event.model = handle;
        event.model_info = *budo_llama_model_info(model);
        event.file_identity_high = identity_high;
        event.file_identity_low = identity_low;
        event.file_size = file_size;
        event.context_size = job.load.context_size;
        event.batch_size = job.load.batch_size;
        if (job.placement_cache_hit && !event.model_info.fallback_reason[0])
            std::snprintf(event.model_info.fallback_reason,
                          sizeof(event.model_info.fallback_reason),
                          "placement cache hit");
        if (used_fallback)
        {
            std::snprintf(event.model_info.backend,
                          sizeof(event.model_info.backend), "cpu");
            std::snprintf(event.model_info.device,
                          sizeof(event.model_info.device), "cpu");
            event.model_info.gpu_layers = 0;
            std::snprintf(event.model_info.fallback_reason,
                          sizeof(event.model_info.fallback_reason),
                          "Accelerated placement failed; loaded on CPU");
        }
        push_event(service, event, true);
        return;
    }
    if (job.type == JobType::CreateChat)
    {
        auto found = service->models.find(job.model);
        if (found == service->models.end())
            return;
        char error[512]{};
        BudoLlamaChat *chat = budo_llama_chat_create(found->second, job.text.c_str(), job.context_size, error, sizeof(error));
        if (!chat)
        {
            push_error(service, 0, job.model, job.handle, error);
            return;
        }
        auto *entry = new ChatEntry();
        entry->chat = chat;
        entry->model = job.model;
        {
            std::lock_guard<std::mutex> lock(service->mutex);
            service->chats[job.handle] = entry;
        }
        return;
    }
    ChatEntry *entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        auto chat_it = service->chats.find(job.handle);
        if (chat_it != service->chats.end())
            entry = chat_it->second;
    }
    if (job.type == JobType::DestroyModel)
    {
        auto model_it = service->models.find(job.handle);
        if (model_it != service->models.end())
        {
            {
                std::lock_guard<std::mutex> lock(service->mutex);
                for (auto child = service->chats.begin(); child != service->chats.end();)
                {
                    if (child->second->model == job.handle)
                    {
                        budo_llama_chat_free(child->second->chat);
                        delete child->second;
                        child = service->chats.erase(child);
                    }
                    else
                    {
                        ++child;
                    }
                }
            }
            budo_llama_model_free(model_it->second);
            service->models.erase(model_it);
        }
        return;
    }
    if (!entry)
    {
        if (job.type == JobType::Generate)
        {
            {
                std::lock_guard<std::mutex> lock(service->mutex);
                service->active_chats.erase(job.handle);
            }
            push_error(service, job.request_id, 0, job.handle,
                       "Chat is unavailable or failed to initialize");
        }
        return;
    }
    if (job.type == JobType::ClearChat)
    {
        budo_llama_chat_clear(entry->chat);
        return;
    }
    if (job.type == JobType::DestroyChat)
    {
        {
            std::lock_guard<std::mutex> lock(service->mutex);
            service->chats.erase(job.handle);
        }
        budo_llama_chat_free(entry->chat);
        delete entry;
        return;
    }
    if (job.type == JobType::Generate)
    {
        entry->running = true;
        entry->cancelled = false;
        uint32_t prompt = 0, generated = 0;
        double prompt_rate = 0, generated_rate = 0;
        bool stopped = false;
        char error[512]{};
        StreamState stream{service, &job};
        bool ok = budo_llama_chat_generate(entry->chat, job.text.c_str(), &job.generate,
                                           generation_cancelled, entry,
                                           stream_text, &stream, &prompt, &generated,
                                           &prompt_rate, &generated_rate,
                                           &stopped, error, sizeof(error));
        entry->running = false;
        {
            std::lock_guard<std::mutex> lock(service->mutex);
            service->active_chats.erase(job.handle);
        }
        if (stream.queue_overflow)
        {
            push_error(service, job.request_id, entry->model, job.handle,
                       "llama.cpp text event queue overflowed");
            return;
        }
        if (stream.invalid_utf8 || !stream.pending.empty())
        {
            push_error(service, job.request_id, entry->model, job.handle,
                       "Model produced invalid UTF-8 text");
            return;
        }
        if (!ok)
        {
            push_error(service, job.request_id, entry->model, job.handle, error);
            return;
        }
        LlamaCppEvent event{};
        event.type = LLAMACPP_EVENT_COMPLETE;
        event.request_id = job.request_id;
        event.model = entry->model;
        event.chat = job.handle;
        event.prompt_tokens = prompt;
        event.generated_tokens = generated;
        event.prompt_tokens_per_second = prompt_rate;
        event.generated_tokens_per_second = generated_rate;
        event.cancelled = entry->cancelled;
        event.stopped = stopped;
        push_event(service, event, true);
    }
}

static BUDO_THREAD_RETURN worker_main(void *opaque)
{
    auto *service = static_cast<LlamaCppService *>(opaque);
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(service->mutex);
            service->wake.wait(lock, [&]
                               { return !service->jobs.empty(); });
            job = std::move(service->jobs.front());
            service->jobs.pop_front();
        }
        if (job.type == JobType::Stop)
            break;
        run_job(service, job);
    }
    return BUDO_THREAD_RESULT;
}

static bool enqueue(LlamaCppService *service, Job job)
{
    if (!service || service->closing)
        return false;
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        if (service->jobs.size() >= 32)
            return false;
        service->jobs.push_back(std::move(job));
    }
    service->wake.notify_one();
    return true;
}

LlamaCppService *llamacpp_service_create(FileContext *files)
{
    if (!files)
        return nullptr;
    auto *service = new LlamaCppService();
    service->files = files;
    if (!subsystem_queue_init(&service->events, service->event_storage, sizeof(LlamaCppEvent), kEventCapacity))
    {
        delete service;
        return nullptr;
    }
    service->worker_ready = budo_thread_create(&service->worker, worker_main, service);
    if (!service->worker_ready)
    {
        subsystem_queue_destroy(&service->events);
        delete service;
        return nullptr;
    }
    return service;
}

void llamacpp_service_destroy(LlamaCppService *service)
{
    if (!service)
        return;
    service->closing = true;
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        for (auto &item : service->chats)
            item.second->cancelled = true;
    }
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        for (Job &job : service->jobs)
        {
            if (job.type == JobType::Load)
            {
                file_native_close(&job.reference);
                file_native_close(&job.fallback_reference);
            }
        }
        service->jobs.clear();
        service->jobs.push_back(Job{JobType::Stop});
    }
    service->wake.notify_one();
    if (service->worker_ready)
        budo_thread_join(service->worker);
    for (auto &item : service->chats)
    {
        budo_llama_chat_free(item.second->chat);
        delete item.second;
    }
    for (auto &item : service->models)
        budo_llama_model_free(item.second);
    LlamaCppEvent event;
    while (subsystem_queue_try_pop(&service->events, &event))
        free_event(event);
    for (auto &terminal : service->terminal_events)
        free_event(terminal);
    subsystem_queue_destroy(&service->events);
    if (service->backend_ready)
        budo_llama_backend_shutdown();
    delete service;
}

bool llamacpp_service_is_available(const LlamaCppService *service) { return service && budo_llama_is_available(); }
size_t llamacpp_service_get_devices(LlamaCppService *service, BudoLlamaDeviceInfo *devices, size_t capacity)
{
    if (!service || service->closing)
        return 0;
    ensure_backend(service);
    return budo_llama_get_devices(devices, capacity);
}
const char *llamacpp_service_get_error(const LlamaCppService *service) { return service ? service->error : ""; }
void llamacpp_service_set_event_callback(LlamaCppService *service, LlamaCppEventCallback callback, void *opaque)
{
    if (service)
    {
        service->callback = callback;
        service->callback_opaque = opaque;
    }
}

bool llamacpp_service_load_model(LlamaCppService *service, uint64_t request_id, const char *path, const BudoLlamaLoadOptions *options)
{
    if (!service || !path || std::strlen(path) >= FILE_MAX_PATH || !options)
        return false;
    Job job{};
    job.type = JobType::Load;
    job.request_id = request_id;
    job.load = *options;
    initialize_native_reference(&job.reference);
    initialize_native_reference(&job.fallback_reference);
    if (!file_native_open(service->files, path, &job.reference))
    {
        std::snprintf(service->error, sizeof(service->error), "%s", file_get_error(service->files));
        return false;
    }
    if (job.reference.size < 4 || job.reference.size > kMaxModelBytes)
    {
        file_native_close(&job.reference);
        std::snprintf(service->error, sizeof(service->error), "Model size is outside the configured limit");
        return false;
    }
    if (options->allow_fallback && options->gpu_layers != 0)
    {
        if (!file_native_open(service->files, path, &job.fallback_reference))
        {
            file_native_close(&job.reference);
            std::snprintf(service->error, sizeof(service->error), "%s",
                          file_get_error(service->files));
            return false;
        }
        job.has_fallback_reference = true;
    }
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        if (service->closing || service->jobs.size() >= 32 ||
            service->outstanding_requests.size() >= 32)
        {
            file_native_close(&job.reference);
            file_native_close(&job.fallback_reference);
            std::snprintf(service->error, sizeof(service->error),
                          "llama.cpp request capacity is full");
            return false;
        }
        service->outstanding_requests.insert(request_id);
        service->jobs.push_back(std::move(job));
    }
    service->wake.notify_one();
    return true;
}

LlamaCppHandle llamacpp_service_create_chat(LlamaCppService *service, LlamaCppHandle model, const char *system_prompt, uint32_t context_size)
{
    if (!service || !model || context_size > 131072)
        return 0;
    Job job{};
    job.type = JobType::CreateChat;
    job.model = model;
    job.text = system_prompt ? system_prompt : "";
    job.context_size = context_size;
    LlamaCppHandle handle;
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        if (service->closing || service->jobs.size() >= 32 ||
            service->live_models.find(model) == service->live_models.end())
            return 0;
        handle = service->next_handle++;
        job.handle = handle;
        service->jobs.push_back(std::move(job));
    }
    service->wake.notify_one();
    return handle;
}

bool llamacpp_service_generate(LlamaCppService *service, uint64_t request_id, LlamaCppHandle chat, const char *text, const BudoLlamaGenerateOptions *options)
{
    if (!service || !chat || !text || std::strlen(text) > kMaxPromptBytes || !options || options->max_tokens > kMaxGeneratedTokens)
        return false;
    Job job{};
    job.type = JobType::Generate;
    job.request_id = request_id;
    job.handle = chat;
    job.text = text;
    job.generate = *options;
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        if (service->active_chats.find(chat) != service->active_chats.end())
            return false;
        if (service->closing || service->jobs.size() >= 32 ||
            service->outstanding_requests.size() >= 32)
            return false;
        service->active_chats.insert(chat);
        service->outstanding_requests.insert(request_id);
        service->jobs.push_back(std::move(job));
    }
    service->wake.notify_one();
    return true;
}
void llamacpp_service_cancel(LlamaCppService *service, LlamaCppHandle chat)
{
    if (service)
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        auto it = service->chats.find(chat);
        if (it != service->chats.end())
            it->second->cancelled = true;
    }
}
void llamacpp_service_cancel_all(LlamaCppService *service)
{
    if (!service)
        return;
    std::lock_guard<std::mutex> lock(service->mutex);
    for (auto &item : service->chats)
        item.second->cancelled = true;
}
void llamacpp_service_clear_chat(LlamaCppService *service, LlamaCppHandle chat)
{
    Job job{};
    job.type = JobType::ClearChat;
    job.handle = chat;
    enqueue(service, std::move(job));
}
void llamacpp_service_destroy_chat(LlamaCppService *service, LlamaCppHandle chat)
{
    llamacpp_service_cancel(service, chat);
    Job job{};
    job.type = JobType::DestroyChat;
    job.handle = chat;
    enqueue(service, std::move(job));
}
void llamacpp_service_destroy_model(LlamaCppService *service, LlamaCppHandle model)
{
    if (service)
    {
        std::lock_guard<std::mutex> lock(service->mutex);
        service->live_models.erase(model);
        for (auto &item : service->chats)
        {
            if (item.second->model == model)
                item.second->cancelled = true;
        }
    }
    Job job{};
    job.type = JobType::DestroyModel;
    job.handle = model;
    enqueue(service, std::move(job));
}

void llamacpp_service_poll(LlamaCppService *service)
{
    if (!service || !service->callback)
        return;
    LlamaCppEvent event;
    while (subsystem_queue_try_pop(&service->events, &event))
    {
        service->callback(&event, service->callback_opaque);
        free_event(event);
    }
    std::deque<LlamaCppEvent> terminal;
    {
        std::lock_guard<std::mutex> lock(service->terminal_mutex);
        terminal.swap(service->terminal_events);
    }
    for (auto &item : terminal)
    {
        publish_placement_cache(service, &item);
        service->callback(&item, service->callback_opaque);
        {
            std::lock_guard<std::mutex> lock(service->mutex);
            service->outstanding_requests.erase(item.request_id);
        }
        free_event(item);
    }
}
