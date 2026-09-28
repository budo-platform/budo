#include "js_llamacpp_bindings.h"
#include "llamacpp_service.h"
#include <quickjs.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <vector>

struct JsLlamaRequest
{
    JSValue callback{JS_UNDEFINED};
    JSValue generation{JS_UNDEFINED};
    std::string text;
    std::string generated_text;
};

struct JsLlamaHandleData
{
    JsLlamaCppContext *state;
    LlamaCppHandle handle;
};

struct JsLlamaCppContext
{
    JSContext *ctx{};
    LlamaCppService *service{};
    uint64_t next_request{1};
    std::unordered_map<uint64_t, JsLlamaRequest> requests;
    std::unordered_map<LlamaCppHandle, BudoLlamaModelInfo> models;
    std::unordered_map<LlamaCppHandle, std::vector<std::string>> chat_messages;
};

static JSValue model_info(JSContext *ctx, const BudoLlamaModelInfo &info,
                          LlamaCppHandle handle);

static JsLlamaCppContext *state_from(JSContext *ctx, JSValueConst value)
{
    size_t size = 0;
    uint8_t *bytes = JS_GetArrayBuffer(ctx, &size, value);
    JsLlamaCppContext *state = nullptr;
    if (bytes && size == sizeof(state))
        std::memcpy(&state, bytes, sizeof(state));
    return state;
}

static JsLlamaHandleData handle_from(JSContext *ctx, JSValueConst value)
{
    size_t size = 0;
    uint8_t *bytes = JS_GetArrayBuffer(ctx, &size, value);
    JsLlamaHandleData handle{};
    if (bytes && size == sizeof(handle))
        std::memcpy(&handle, bytes, sizeof(handle));
    return handle;
}

static JSValue handle_data(JSContext *ctx, JsLlamaCppContext *state, LlamaCppHandle handle)
{
    JsLlamaHandleData data{state, handle};
    return JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t *>(&data), sizeof(data));
}

static JSValue js_model_get_info(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    auto found = handle.state->models.find(handle.handle);
    return found == handle.state->models.end() ? JS_NULL : model_info(ctx, found->second, handle.handle);
}

static JSValue js_chat_send(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data);
static JSValue js_chat_cancel(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    llamacpp_service_cancel(handle.state->service, handle.handle);
    return JS_UNDEFINED;
}
static JSValue js_chat_clear(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    handle.state->chat_messages[handle.handle].clear();
    llamacpp_service_clear_chat(handle.state->service, handle.handle);
    return JS_UNDEFINED;
}
static JSValue js_chat_destroy(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    handle.state->chat_messages.erase(handle.handle);
    llamacpp_service_destroy_chat(handle.state->service, handle.handle);
    return JS_UNDEFINED;
}
static JSValue js_chat_messages(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    JSValue array = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const std::string &message : handle.state->chat_messages[handle.handle])
    {
        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "role", JS_NewString(ctx, index % 2 ? "assistant" : "user"));
        JS_SetPropertyStr(ctx, item, "content", JS_NewStringLen(ctx, message.data(), message.size()));
        JS_SetPropertyUint32(ctx, array, index++, item);
    }
    return array;
}

static JSValue make_chat(JSContext *ctx, JsLlamaCppContext *state, LlamaCppHandle handle)
{
    JSValue object = JS_NewObject(ctx);
    JSValue data = handle_data(ctx, state, handle);
    auto add = [&](const char *name, JSCFunctionData *function, int length)
    { JS_SetPropertyStr(ctx, object, name, JS_NewCFunctionData(ctx, function, length, 0, 1, &data)); };
    add("send", js_chat_send, 3);
    add("getMessages", js_chat_messages, 0);
    add("clear", js_chat_clear, 0);
    add("cancel", js_chat_cancel, 0);
    add("destroy", js_chat_destroy, 0);
    JS_FreeValue(ctx, data);
    return object;
}

static JSValue js_model_create_chat(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    const char *system_prompt = "";
    uint32_t context_size = 0;
    JSValue prompt_value = JS_UNDEFINED;
    if (argc && JS_IsObject(argv[0]))
    {
        prompt_value = JS_GetPropertyStr(ctx, argv[0], "systemPrompt");
        if (JS_IsString(prompt_value))
            system_prompt = JS_ToCString(ctx, prompt_value);
        JSValue context = JS_GetPropertyStr(ctx, argv[0], "contextSize");
        if (!JS_IsUndefined(context) && JS_ToUint32(ctx, &context_size, context))
        {
            JS_FreeValue(ctx, context);
            JS_FreeValue(ctx, prompt_value);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, context);
    }
    LlamaCppHandle chat = llamacpp_service_create_chat(handle.state->service, handle.handle, system_prompt, context_size);
    if (system_prompt[0])
        JS_FreeCString(ctx, system_prompt);
    JS_FreeValue(ctx, prompt_value);
    if (!chat)
        return JS_ThrowInternalError(ctx, "Cannot create llama.cpp chat");
    handle.state->chat_messages.emplace(chat, std::vector<std::string>{});
    return make_chat(ctx, handle.state, chat);
}

static JSValue js_model_destroy(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    handle.state->models.erase(handle.handle);
    llamacpp_service_destroy_model(handle.state->service, handle.handle);
    return JS_UNDEFINED;
}

static JSValue make_model(JSContext *ctx, JsLlamaCppContext *state, LlamaCppHandle handle)
{
    JSValue object = JS_NewObject(ctx);
    JSValue data = handle_data(ctx, state, handle);
    JS_SetPropertyStr(ctx, object, "getInfo", JS_NewCFunctionData(ctx, js_model_get_info, 0, 0, 1, &data));
    JS_SetPropertyStr(ctx, object, "createChat", JS_NewCFunctionData(ctx, js_model_create_chat, 1, 0, 1, &data));
    JS_SetPropertyStr(ctx, object, "destroy", JS_NewCFunctionData(ctx, js_model_destroy, 0, 0, 1, &data));
    JS_FreeValue(ctx, data);
    return object;
}

static JSValue js_generation_cancel(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    return js_chat_cancel(ctx, JS_UNDEFINED, 0, nullptr, 1, data);
}

static bool read_number(JSContext *ctx, JSValueConst object, const char *name,
                        double *value, bool *present)
{
    JSValue property = JS_GetPropertyStr(ctx, object, name);
    *present = !JS_IsUndefined(property);
    if (JS_IsUndefined(property))
    {
        JS_FreeValue(ctx, property);
        return true;
    }
    int result = JS_ToFloat64(ctx, value, property);
    JS_FreeValue(ctx, property);
    return result == 0 && std::isfinite(*value);
}

static bool parse_generate_options(JSContext *ctx, JSValueConst object,
                                   BudoLlamaGenerateOptions *options)
{
    double value;
    bool present;
    if (!read_number(ctx, object, "maxTokens", &value, &present) ||
        (present && (value < 1 || value > 8192 || value != std::floor(value))))
        return false;
    if (present)
        options->max_tokens = (uint32_t)value;
    if (!read_number(ctx, object, "minTokens", &value, &present) ||
        (present && (value < 0 || value > options->max_tokens ||
                     value != std::floor(value))))
        return false;
    if (present)
        options->min_tokens = (uint32_t)value;
    if (!read_number(ctx, object, "temperature", &value, &present) ||
        (present && (value < 0 || value > 10)))
        return false;
    if (present)
        options->temperature = (float)value;
    if (!read_number(ctx, object, "topK", &value, &present) ||
        (present && (value < 0 || value > 100000 || value != std::floor(value))))
        return false;
    if (present)
        options->top_k = (int32_t)value;
    if (!read_number(ctx, object, "topP", &value, &present) || (present && (value < 0 || value > 1)))
        return false;
    if (present)
        options->top_p = (float)value;
    if (!read_number(ctx, object, "minP", &value, &present) || (present && (value < 0 || value > 1)))
        return false;
    if (present)
        options->min_p = (float)value;
    if (!read_number(ctx, object, "repetitionPenalty", &value, &present) || (present && (value <= 0 || value > 10)))
        return false;
    if (present)
        options->repetition_penalty = (float)value;
    if (!read_number(ctx, object, "seed", &value, &present) ||
        (present && (value < 0 || value > UINT32_MAX || value != std::floor(value))))
        return false;
    if (present)
    {
        options->seed = (uint32_t)value;
        options->random_seed = false;
    }
    JSValue property = JS_GetPropertyStr(ctx, object, "stop");
    if (!JS_IsUndefined(property))
    {
        int64_t count = 0;
        if (JS_GetLength(ctx, property, &count) || count < 0 || count > 8)
        {
            JS_FreeValue(ctx, property);
            return false;
        }
        options->stop_count = (uint32_t)count;
        for (uint32_t index = 0; index < (uint32_t)count; ++index)
        {
            JSValue item = JS_GetPropertyUint32(ctx, property, index);
            const char *stop = JS_ToCString(ctx, item);
            if (!stop || !stop[0] || std::strlen(stop) >= sizeof(options->stops[index]))
            {
                if (stop)
                    JS_FreeCString(ctx, stop);
                JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, property);
                return false;
            }
            std::snprintf(options->stops[index], sizeof(options->stops[index]), "%s", stop);
            JS_FreeCString(ctx, stop);
            JS_FreeValue(ctx, item);
        }
    }
    JS_FreeValue(ctx, property);
    return true;
}

static JSValue js_chat_send(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data)
{
    JsLlamaHandleData handle = handle_from(ctx, data[0]);
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "send requires text and callbacks");
    int callback_index = JS_IsObject(argv[1]) && (argc < 3 || JS_IsUndefined(argv[2])) ? 1 : 2;
    if (argc <= callback_index || !JS_IsObject(argv[callback_index]))
        return JS_ThrowTypeError(ctx, "callbacks must be an object");
    const char *text = JS_ToCString(ctx, argv[0]);
    if (!text)
        return JS_EXCEPTION;
    BudoLlamaGenerateOptions options = budo_llama_default_generate_options();
    if (callback_index == 2 && JS_IsObject(argv[1]) &&
        !parse_generate_options(ctx, argv[1], &options))
    {
        JS_FreeCString(ctx, text);
        return JS_ThrowRangeError(ctx, "Invalid llama.cpp generation options");
    }
    uint64_t request = handle.state->next_request++;
    JSValue generation = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, generation, "running", JS_TRUE);
    JS_SetPropertyStr(ctx, generation, "cancel", JS_NewCFunctionData(ctx, js_generation_cancel, 0, 0, 1, data));
    if (!llamacpp_service_generate(handle.state->service, request, handle.handle, text, &options))
    {
        JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, generation);
        return JS_ThrowInternalError(ctx, "Cannot queue llama.cpp generation");
    }
    JsLlamaRequest pending;
    pending.callback = JS_DupValue(ctx, argv[callback_index]);
    pending.generation = JS_DupValue(ctx, generation);
    pending.text = text;
    handle.state->requests.emplace(request, std::move(pending));
    handle.state->chat_messages[handle.handle].emplace_back(text);
    JS_FreeCString(ctx, text);
    return generation;
}

static JSValue model_info(JSContext *ctx, const BudoLlamaModelInfo &info, LlamaCppHandle handle)
{
    JSValue object = JS_NewObject(ctx);
#define SET_STRING(name, value) JS_SetPropertyStr(ctx, object, name, JS_NewString(ctx, value))
#define SET_INT(name, value) JS_SetPropertyStr(ctx, object, name, JS_NewInt64(ctx, value))
    SET_INT("handle", handle);
    SET_STRING("name", info.name);
    SET_STRING("architecture", info.architecture);
    SET_STRING("quantization", info.quantization);
    SET_INT("parameterCount", info.parameter_count);
    SET_INT("fileSize", info.file_size);
    SET_INT("modelContextSize", info.model_context_size);
    SET_INT("activeContextSize", info.active_context_size);
    SET_INT("vocabularySize", info.vocabulary_size);
    SET_STRING("chatTemplate", info.chat_template);
    SET_STRING("backend", info.backend);
    SET_STRING("device", info.device);
    SET_INT("gpuLayers", info.gpu_layers);
    SET_INT("totalLayers", info.total_layers);
    SET_INT("estimatedMemoryBytes", info.estimated_memory_bytes);
    SET_STRING("fallbackReason", info.fallback_reason);
#undef SET_STRING
#undef SET_INT
    return object;
}

static void on_event(const LlamaCppEvent *event, void *opaque)
{
    auto *state = static_cast<JsLlamaCppContext *>(opaque);
    auto found = state->requests.find(event->request_id);
    if (found == state->requests.end())
        return;
    JsLlamaRequest &pending = found->second;
    if (event->type == LLAMACPP_EVENT_TEXT)
    {
        JSValue function = JS_GetPropertyStr(state->ctx, pending.callback, "onText");
        JSValue text = JS_NewStringLen(state->ctx, event->text, event->text_length);
        pending.generated_text.append(event->text, event->text_length);
        if (JS_IsFunction(state->ctx, function))
        {
            JSValue result = JS_Call(state->ctx, function, pending.callback, 1, &text);
            JS_FreeValue(state->ctx, result);
        }
        JS_FreeValue(state->ctx, text);
        JS_FreeValue(state->ctx, function);
        return;
    }
    if (event->type == LLAMACPP_EVENT_MODEL_LOADED)
    {
        state->models[event->model] = event->model_info;
        JSValue args[2] = {make_model(state->ctx, state, event->model), JS_NULL};
        JSValue result = JS_Call(state->ctx, pending.callback, JS_UNDEFINED, 2, args);
        JS_FreeValue(state->ctx, result);
        JS_FreeValue(state->ctx, args[0]);
    }
    else
    {
        if (!JS_IsUndefined(pending.generation))
            JS_SetPropertyStr(state->ctx, pending.generation, "running", JS_FALSE);
        const char *name = event->type == LLAMACPP_EVENT_COMPLETE ? "onComplete" : "onError";
        JSValue function = JS_GetPropertyStr(state->ctx, pending.callback, name);
        JSValue argument = JS_NewObject(state->ctx);
        if (event->type == LLAMACPP_EVENT_COMPLETE)
        {
            JS_SetPropertyStr(state->ctx, argument, "finishReason", JS_NewString(state->ctx, event->cancelled ? "cancelled" : event->stopped ? "stop"
                                                                                                                                             : "length"));
            JS_SetPropertyStr(state->ctx, argument, "promptTokens", JS_NewInt32(state->ctx, event->prompt_tokens));
            JS_SetPropertyStr(state->ctx, argument, "generatedTokens", JS_NewInt32(state->ctx, event->generated_tokens));
            JS_SetPropertyStr(state->ctx, argument, "text", JS_NewStringLen(state->ctx, pending.generated_text.data(), pending.generated_text.size()));
            JS_SetPropertyStr(state->ctx, argument, "promptTokensPerSecond",
                              JS_NewFloat64(state->ctx, event->prompt_tokens_per_second));
            JS_SetPropertyStr(state->ctx, argument, "generatedTokensPerSecond",
                              JS_NewFloat64(state->ctx, event->generated_tokens_per_second));
            state->chat_messages[event->chat].emplace_back(pending.generated_text);
        }
        else
            JS_SetPropertyStr(state->ctx, argument, "message", JS_NewStringLen(state->ctx, event->text, event->text_length));
        if (JS_IsFunction(state->ctx, function))
        {
            JSValue result = JS_Call(state->ctx, function, pending.callback, 1, &argument);
            JS_FreeValue(state->ctx, result);
        }
        JS_FreeValue(state->ctx, argument);
        JS_FreeValue(state->ctx, function);
    }
    JS_FreeValue(state->ctx, pending.callback);
    JS_FreeValue(state->ctx, pending.generation);
    state->requests.erase(found);
}

static JSValue js_available(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    auto *state = state_from(ctx, data[0]);
    return JS_NewBool(ctx, state && llamacpp_service_is_available(state->service));
}
static JSValue js_error(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *data)
{
    auto *state = state_from(ctx, data[0]);
    return JS_NewString(ctx, state ? llamacpp_service_get_error(state->service) : "");
}
static JSValue js_devices(JSContext *ctx, JSValueConst, int, JSValueConst *, int, JSValue *)
{
    JSValue array = JS_NewArray(ctx);
    BudoLlamaDeviceInfo devices[16];
    size_t count = std::min(budo_llama_get_devices(devices, 16), (size_t)16);
    for (uint32_t index = 0; index < count; ++index)
    {
        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "id", JS_NewString(ctx, devices[index].id));
        JS_SetPropertyStr(ctx, item, "backend", JS_NewString(ctx, devices[index].backend));
        JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, devices[index].name));
        JS_SetPropertyStr(ctx, item, "available", JS_NewBool(ctx, devices[index].available));
        JS_SetPropertyStr(ctx, item, "memoryBytes", devices[index].has_memory ? JS_NewInt64(ctx, (int64_t)devices[index].memory_bytes) : JS_NULL);
        JS_SetPropertyStr(ctx, item, "reason", JS_NewString(ctx, devices[index].reason));
        JS_SetPropertyUint32(ctx, array, index, item);
    }
    return array;
}

static bool parse_load_options(JSContext *ctx, JSValueConst object,
                               BudoLlamaLoadOptions *options)
{
    JSValue property = JS_GetPropertyStr(ctx, object, "device");
    if (!JS_IsUndefined(property))
    {
        const char *device = JS_ToCString(ctx, property);
        bool supported = device && (std::strcmp(device, "auto") == 0 ||
                                    std::strcmp(device, "cpu") == 0);
        if (device && !supported)
        {
            BudoLlamaDeviceInfo devices[16];
            size_t count = std::min(budo_llama_get_devices(devices, 16), (size_t)16);
            for (size_t index = 0; index < count; ++index)
                supported = supported || std::strcmp(device, devices[index].id) == 0;
        }
        if (!supported)
        {
            if (device)
                JS_FreeCString(ctx, device);
            JS_FreeValue(ctx, property);
            return false;
        }
        std::snprintf(options->device, sizeof(options->device), "%s", device);
        if (std::strcmp(device, "cpu") == 0)
            options->gpu_layers = 0;
        options->allow_fallback = std::strcmp(device, "auto") == 0;
        JS_FreeCString(ctx, device);
    }
    JS_FreeValue(ctx, property);
    double value;
    bool present;
    if (!read_number(ctx, object, "contextSize", &value, &present) ||
        (present && (value < 128 || value > 131072 || value != std::floor(value))))
        return false;
    if (present)
        options->context_size = (uint32_t)value;
    property = JS_GetPropertyStr(ctx, object, "gpuLayers");
    if (JS_IsString(property))
    {
        const char *automatic = JS_ToCString(ctx, property);
        bool valid = automatic && std::strcmp(automatic, "auto") == 0;
        if (automatic)
            JS_FreeCString(ctx, automatic);
        JS_FreeValue(ctx, property);
        if (!valid)
            return false;
        present = false;
    }
    else
    {
        JS_FreeValue(ctx, property);
        if (!read_number(ctx, object, "gpuLayers", &value, &present) ||
            (present && (value < 0 || value > INT32_MAX || value != std::floor(value))))
            return false;
    }
    if (present)
        options->gpu_layers = (int32_t)value;
    property = JS_GetPropertyStr(ctx, object, "threads");
    if (JS_IsString(property))
    {
        const char *automatic = JS_ToCString(ctx, property);
        bool valid = automatic && std::strcmp(automatic, "auto") == 0;
        if (automatic)
            JS_FreeCString(ctx, automatic);
        JS_FreeValue(ctx, property);
        if (!valid)
            return false;
        present = false;
    }
    else
    {
        JS_FreeValue(ctx, property);
        if (!read_number(ctx, object, "threads", &value, &present) || (present && (value < 1 || value > 256 || value != std::floor(value))))
            return false;
    }
    if (present)
        options->threads = (int32_t)value;
    property = JS_GetPropertyStr(ctx, object, "batchSize");
    if (JS_IsString(property))
    {
        const char *automatic = JS_ToCString(ctx, property);
        bool valid = automatic && std::strcmp(automatic, "auto") == 0;
        if (automatic)
            JS_FreeCString(ctx, automatic);
        JS_FreeValue(ctx, property);
        if (!valid)
            return false;
        present = false;
    }
    else
    {
        JS_FreeValue(ctx, property);
        if (!read_number(ctx, object, "batchSize", &value, &present) || (present && (value < 1 || value > 4096 || value != std::floor(value))))
            return false;
    }
    if (present)
        options->batch_size = (int32_t)value;
    property = JS_GetPropertyStr(ctx, object, "useMmap");
    if (!JS_IsUndefined(property))
    {
        int boolean = JS_ToBool(ctx, property);
        if (boolean < 0)
        {
            JS_FreeValue(ctx, property);
            return false;
        }
        options->use_mmap = boolean != 0;
    }
    JS_FreeValue(ctx, property);
    property = JS_GetPropertyStr(ctx, object, "allowFallback");
    if (!JS_IsUndefined(property))
    {
        int boolean = JS_ToBool(ctx, property);
        if (boolean < 0)
        {
            JS_FreeValue(ctx, property);
            return false;
        }
        options->allow_fallback = boolean != 0;
    }
    JS_FreeValue(ctx, property);
    return true;
}

static JSValue js_load(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int, JSValue *data)
{
    auto *state = state_from(ctx, data[0]);
    if (!state || argc < 2)
        return JS_ThrowTypeError(ctx, "loadModel requires path and callback");
    int callback_index = JS_IsFunction(ctx, argv[1]) ? 1 : 2;
    if (argc <= callback_index || !JS_IsFunction(ctx, argv[callback_index]))
        return JS_ThrowTypeError(ctx, "callback must be a function");
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    BudoLlamaLoadOptions options = budo_llama_default_load_options();
    if (callback_index == 2 && (!JS_IsObject(argv[1]) ||
                                !parse_load_options(ctx, argv[1], &options)))
    {
        JS_FreeCString(ctx, path);
        return JS_ThrowRangeError(ctx, "Invalid llama.cpp load options");
    }
    uint64_t request = state->next_request++;
    bool queued = llamacpp_service_load_model(state->service, request, path, &options);
    JS_FreeCString(ctx, path);
    if (!queued)
        return JS_ThrowInternalError(ctx, "%s", llamacpp_service_get_error(state->service));
    JsLlamaRequest pending;
    pending.callback = JS_DupValue(ctx, argv[callback_index]);
    state->requests.emplace(request, std::move(pending));
    return JS_UNDEFINED;
}

struct JsLlamaApiFunction
{
    const char *name;
    int length;
    JSCFunctionData *callback;
};

static const JsLlamaApiFunction js_llamacpp_functions[] = {
    {"isAvailable", 0, js_available},
    {"getDevices", 0, js_devices},
    {"loadModel", 3, js_load},
    {"getError", 0, js_error},
};

JsLlamaCppContext *js_llamacpp_init(JSContext *ctx, FileContext *files)
{
    auto *state = new JsLlamaCppContext();
    state->ctx = ctx;
    state->service = llamacpp_service_create(files);
    if (!state->service)
    {
        delete state;
        return nullptr;
    }
    llamacpp_service_set_event_callback(state->service, on_event, state);
    JSValue global = JS_GetGlobalObject(ctx), sys = JS_GetPropertyStr(ctx, global, "sys"), api = JS_NewObject(ctx);
    JSValue data = JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t *>(&state), sizeof(state));
    for (const auto &function : js_llamacpp_functions)
    {
        JSValue value = JS_NewCFunctionData(ctx, function.callback, function.length,
                                            0, 1, &data);
        JS_SetPropertyStr(ctx, api, function.name, value);
    }
    JS_SetPropertyStr(ctx, sys, "llamacpp", api);
    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, sys);
    JS_FreeValue(ctx, global);
    return state;
}
void js_llamacpp_poll(JsLlamaCppContext *state)
{
    if (state)
        llamacpp_service_poll(state->service);
}
void js_llamacpp_cancel_all(JsLlamaCppContext *state)
{
    if (state)
        llamacpp_service_cancel_all(state->service);
}
void js_llamacpp_cleanup(JsLlamaCppContext *state)
{
    if (!state)
        return;
    for (auto &item : state->requests)
    {
        JS_FreeValue(state->ctx, item.second.callback);
        JS_FreeValue(state->ctx, item.second.generation);
    }
    state->requests.clear();
    llamacpp_service_set_event_callback(state->service, nullptr, nullptr);
    llamacpp_service_destroy(state->service);
    delete state;
}
