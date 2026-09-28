#include "lua_llamacpp_bindings.h"
#include "llamacpp_service.h"
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

enum class LuaLlamaRequestType
{
    Load,
    Generate
};

struct LuaLlamaRequest
{
    LuaLlamaRequestType type{LuaLlamaRequestType::Load};
    int callback_ref{LUA_NOREF};
    LlamaCppHandle chat{};
    std::string generated_text;
};

struct LuaLlamaCppContext
{
    lua_State *L{};
    LlamaCppService *service{};
    uint64_t next_request{1};
    std::unordered_map<uint64_t, LuaLlamaRequest> requests;
    std::unordered_map<LlamaCppHandle, BudoLlamaModelInfo> models;
    std::unordered_map<LlamaCppHandle, std::vector<std::string>> chat_messages;
};

struct LuaLlamaHandleData
{
    LuaLlamaCppContext *state;
    LlamaCppHandle handle;
};

static LuaLlamaCppContext *binding(lua_State *L)
{
    return static_cast<LuaLlamaCppContext *>(
        lua_touserdata(L, lua_upvalueindex(1)));
}

static LuaLlamaHandleData handle_data(lua_State *L)
{
    auto *data = static_cast<LuaLlamaHandleData *>(
        lua_touserdata(L, lua_upvalueindex(1)));
    return data ? *data : LuaLlamaHandleData{};
}

static void push_handle_closure(lua_State *L, LuaLlamaCppContext *state,
                                LlamaCppHandle handle, lua_CFunction function)
{
    auto *data = static_cast<LuaLlamaHandleData *>(
        lua_newuserdatauv(L, sizeof(LuaLlamaHandleData), 0));
    *data = {state, handle};
    lua_pushcclosure(L, function, 1);
}

static void push_model_info(lua_State *L, const BudoLlamaModelInfo &info)
{
    lua_createtable(L, 0, 15);
#define SET_STRING(name, value) \
    lua_pushstring(L, value);   \
    lua_setfield(L, -2, name)
#define SET_INTEGER(name, value)            \
    lua_pushinteger(L, (lua_Integer)value); \
    lua_setfield(L, -2, name)
    SET_STRING("name", info.name);
    SET_STRING("architecture", info.architecture);
    SET_STRING("quantization", info.quantization);
    SET_INTEGER("parameterCount", info.parameter_count);
    SET_INTEGER("fileSize", info.file_size);
    SET_INTEGER("modelContextSize", info.model_context_size);
    SET_INTEGER("activeContextSize", info.active_context_size);
    SET_INTEGER("vocabularySize", info.vocabulary_size);
    SET_STRING("chatTemplate", info.chat_template);
    SET_STRING("backend", info.backend);
    SET_STRING("device", info.device);
    SET_INTEGER("gpuLayers", info.gpu_layers);
    SET_INTEGER("totalLayers", info.total_layers);
    SET_INTEGER("estimatedMemoryBytes", info.estimated_memory_bytes);
    SET_STRING("fallbackReason", info.fallback_reason);
#undef SET_INTEGER
#undef SET_STRING
}

static int l_model_get_info(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    auto found = data.state->models.find(data.handle);
    if (found == data.state->models.end())
    {
        lua_pushnil(L);
        return 1;
    }
    push_model_info(L, found->second);
    return 1;
}

static int l_chat_cancel(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    llamacpp_service_cancel(data.state->service, data.handle);
    return 0;
}

static int l_chat_clear(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    data.state->chat_messages[data.handle].clear();
    llamacpp_service_clear_chat(data.state->service, data.handle);
    return 0;
}

static int l_chat_destroy(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    data.state->chat_messages.erase(data.handle);
    llamacpp_service_destroy_chat(data.state->service, data.handle);
    return 0;
}

static int l_chat_get_messages(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    lua_newtable(L);
    int index = 1;
    for (const std::string &message : data.state->chat_messages[data.handle])
    {
        lua_createtable(L, 0, 2);
        lua_pushstring(L, index % 2 == 0 ? "assistant" : "user");
        lua_setfield(L, -2, "role");
        lua_pushlstring(L, message.data(), message.size());
        lua_setfield(L, -2, "content");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

static bool finite_number(lua_State *L, int table, const char *field,
                          double *value, bool *present)
{
    lua_getfield(L, table, field);
    *present = !lua_isnil(L, -1);
    if (!*present)
    {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isnumber(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    *value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return std::isfinite(*value);
}

static bool parse_generate_options(lua_State *L, int table,
                                   BudoLlamaGenerateOptions *options)
{
    double value;
    bool present;
    if (!finite_number(L, table, "maxTokens", &value, &present) ||
        (present && (value < 1 || value > 8192 || value != std::floor(value))))
        return false;
    if (present)
        options->max_tokens = (uint32_t)value;
    if (!finite_number(L, table, "minTokens", &value, &present) ||
        (present && (value < 0 || value > options->max_tokens ||
                     value != std::floor(value))))
        return false;
    if (present)
        options->min_tokens = (uint32_t)value;
    if (!finite_number(L, table, "temperature", &value, &present) ||
        (present && (value < 0 || value > 10)))
        return false;
    if (present)
        options->temperature = (float)value;
    if (!finite_number(L, table, "topK", &value, &present) ||
        (present && (value < 0 || value > 100000 || value != std::floor(value))))
        return false;
    if (present)
        options->top_k = (int32_t)value;
    if (!finite_number(L, table, "topP", &value, &present) ||
        (present && (value < 0 || value > 1)))
        return false;
    if (present)
        options->top_p = (float)value;
    if (!finite_number(L, table, "minP", &value, &present) ||
        (present && (value < 0 || value > 1)))
        return false;
    if (present)
        options->min_p = (float)value;
    if (!finite_number(L, table, "repetitionPenalty", &value, &present) ||
        (present && (value <= 0 || value > 10)))
        return false;
    if (present)
        options->repetition_penalty = (float)value;
    if (!finite_number(L, table, "seed", &value, &present) ||
        (present && (value < 0 || value > UINT32_MAX || value != std::floor(value))))
        return false;
    if (present)
    {
        options->seed = (uint32_t)value;
        options->random_seed = false;
    }
    lua_getfield(L, table, "stop");
    if (!lua_isnil(L, -1))
    {
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }
        size_t count = lua_rawlen(L, -1);
        if (count > 8)
        {
            lua_pop(L, 1);
            return false;
        }
        options->stop_count = (uint32_t)count;
        for (size_t index = 1; index <= count; ++index)
        {
            lua_rawgeti(L, -1, (lua_Integer)index);
            size_t length = 0;
            const char *stop = lua_tolstring(L, -1, &length);
            if (!stop || !length || length >= sizeof(options->stops[index - 1]))
            {
                lua_pop(L, 2);
                return false;
            }
            std::memcpy(options->stops[index - 1], stop, length);
            options->stops[index - 1][length] = '\0';
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return true;
}

static int l_chat_send(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    size_t text_length = 0;
    const char *text = luaL_checklstring(L, 1, &text_length);
    int callbacks_index = lua_istable(L, 2) && lua_gettop(L) == 2 ? 2 : 3;
    luaL_checktype(L, callbacks_index, LUA_TTABLE);
    BudoLlamaGenerateOptions options = budo_llama_default_generate_options();
    if (callbacks_index == 3)
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        if (!parse_generate_options(L, 2, &options))
            return luaL_error(L, "invalid llama.cpp generation options");
    }
    uint64_t request = data.state->next_request++;
    if (!llamacpp_service_generate(data.state->service, request, data.handle,
                                   text, &options))
        return luaL_error(L, "cannot queue llama.cpp generation");
    lua_pushvalue(L, callbacks_index);
    LuaLlamaRequest pending;
    pending.type = LuaLlamaRequestType::Generate;
    pending.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    pending.chat = data.handle;
    data.state->requests.emplace(request, std::move(pending));
    data.state->chat_messages[data.handle].emplace_back(text, text_length);
    lua_createtable(L, 0, 2);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "running");
    push_handle_closure(L, data.state, data.handle, l_chat_cancel);
    lua_setfield(L, -2, "cancel");
    return 1;
}

static void push_chat(lua_State *L, LuaLlamaCppContext *state,
                      LlamaCppHandle handle)
{
    lua_createtable(L, 0, 5);
    push_handle_closure(L, state, handle, l_chat_send);
    lua_setfield(L, -2, "send");
    push_handle_closure(L, state, handle, l_chat_get_messages);
    lua_setfield(L, -2, "getMessages");
    push_handle_closure(L, state, handle, l_chat_clear);
    lua_setfield(L, -2, "clear");
    push_handle_closure(L, state, handle, l_chat_cancel);
    lua_setfield(L, -2, "cancel");
    push_handle_closure(L, state, handle, l_chat_destroy);
    lua_setfield(L, -2, "destroy");
}

static int l_model_create_chat(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    const char *system_prompt = "";
    uint32_t context_size = 0;
    if (lua_istable(L, 1))
    {
        lua_getfield(L, 1, "systemPrompt");
        if (!lua_isnil(L, -1))
            system_prompt = luaL_checkstring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "contextSize");
        if (!lua_isnil(L, -1))
        {
            lua_Integer value = luaL_checkinteger(L, -1);
            if (value < 128 || value > 131072)
                return luaL_error(L, "contextSize is out of range");
            context_size = (uint32_t)value;
        }
        lua_pop(L, 1);
    }
    LlamaCppHandle chat = llamacpp_service_create_chat(
        data.state->service, data.handle, system_prompt, context_size);
    if (!chat)
        return luaL_error(L, "cannot create llama.cpp chat");
    data.state->chat_messages.emplace(chat, std::vector<std::string>{});
    push_chat(L, data.state, chat);
    return 1;
}

static int l_model_destroy(lua_State *L)
{
    LuaLlamaHandleData data = handle_data(L);
    data.state->models.erase(data.handle);
    llamacpp_service_destroy_model(data.state->service, data.handle);
    return 0;
}

static void push_model(lua_State *L, LuaLlamaCppContext *state,
                       LlamaCppHandle handle)
{
    lua_createtable(L, 0, 3);
    push_handle_closure(L, state, handle, l_model_get_info);
    lua_setfield(L, -2, "getInfo");
    push_handle_closure(L, state, handle, l_model_create_chat);
    lua_setfield(L, -2, "createChat");
    push_handle_closure(L, state, handle, l_model_destroy);
    lua_setfield(L, -2, "destroy");
}

static int l_available(lua_State *L)
{
    lua_pushboolean(L, llamacpp_service_is_available(binding(L)->service));
    return 1;
}

static int l_error(lua_State *L)
{
    lua_pushstring(L, llamacpp_service_get_error(binding(L)->service));
    return 1;
}

static int l_devices(lua_State *L)
{
    lua_newtable(L);
    BudoLlamaDeviceInfo devices[16];
    size_t count = std::min(budo_llama_get_devices(devices, 16), (size_t)16);
    for (size_t index = 0; index < count; ++index)
    {
        lua_createtable(L, 0, 6);
        lua_pushstring(L, devices[index].id);
        lua_setfield(L, -2, "id");
        lua_pushstring(L, devices[index].backend);
        lua_setfield(L, -2, "backend");
        lua_pushstring(L, devices[index].name);
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, devices[index].available);
        lua_setfield(L, -2, "available");
        if (devices[index].has_memory)
            lua_pushinteger(L, (lua_Integer)devices[index].memory_bytes);
        else
            lua_pushnil(L);
        lua_setfield(L, -2, "memoryBytes");
        lua_pushstring(L, devices[index].reason);
        lua_setfield(L, -2, "reason");
        lua_rawseti(L, -2, (lua_Integer)index + 1);
    }
    return 1;
}

static bool parse_load_options(lua_State *L, int table,
                               BudoLlamaLoadOptions *options)
{
    lua_getfield(L, table, "device");
    if (!lua_isnil(L, -1))
    {
        const char *device = lua_tostring(L, -1);
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
            lua_pop(L, 1);
            return false;
        }
        std::snprintf(options->device, sizeof(options->device), "%s", device);
        if (std::strcmp(device, "cpu") == 0)
            options->gpu_layers = 0;
        options->allow_fallback = std::strcmp(device, "auto") == 0;
    }
    lua_pop(L, 1);
    double value;
    bool present;
    if (!finite_number(L, table, "contextSize", &value, &present) ||
        (present && (value < 128 || value > 131072 || value != std::floor(value))))
        return false;
    if (present)
        options->context_size = (uint32_t)value;
    if (!finite_number(L, table, "gpuLayers", &value, &present) ||
        (present && (value < 0 || value > INT32_MAX || value != std::floor(value))))
    {
        lua_getfield(L, table, "gpuLayers");
        bool automatic = lua_isstring(L, -1) &&
                         std::strcmp(lua_tostring(L, -1), "auto") == 0;
        lua_pop(L, 1);
        if (!automatic)
            return false;
        present = false;
    }
    if (present)
        options->gpu_layers = (int32_t)value;
    if (!finite_number(L, table, "threads", &value, &present) ||
        (present && (value < 1 || value > 256 || value != std::floor(value))))
    {
        lua_getfield(L, table, "threads");
        bool automatic = lua_isstring(L, -1) &&
                         std::strcmp(lua_tostring(L, -1), "auto") == 0;
        lua_pop(L, 1);
        if (!automatic)
            return false;
        present = false;
    }
    if (present)
        options->threads = (int32_t)value;
    if (!finite_number(L, table, "batchSize", &value, &present) ||
        (present && (value < 1 || value > 4096 || value != std::floor(value))))
    {
        lua_getfield(L, table, "batchSize");
        bool automatic = lua_isstring(L, -1) &&
                         std::strcmp(lua_tostring(L, -1), "auto") == 0;
        lua_pop(L, 1);
        if (!automatic)
            return false;
        present = false;
    }
    if (present)
        options->batch_size = (int32_t)value;
    lua_getfield(L, table, "useMmap");
    if (!lua_isnil(L, -1))
    {
        if (!lua_isboolean(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }
        options->use_mmap = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    lua_getfield(L, table, "allowFallback");
    if (!lua_isnil(L, -1))
    {
        if (!lua_isboolean(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }
        options->allow_fallback = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    return true;
}

static int l_load(lua_State *L)
{
    auto *state = binding(L);
    const char *path = luaL_checkstring(L, 1);
    int callback_index = lua_isfunction(L, 2) ? 2 : 3;
    luaL_checktype(L, callback_index, LUA_TFUNCTION);
    BudoLlamaLoadOptions options = budo_llama_default_load_options();
    if (callback_index == 3)
    {
        luaL_checktype(L, 2, LUA_TTABLE);
        if (!parse_load_options(L, 2, &options))
            return luaL_error(L, "invalid llama.cpp load options");
    }
    uint64_t request = state->next_request++;
    if (!llamacpp_service_load_model(state->service, request, path, &options))
        return luaL_error(L, "%s", llamacpp_service_get_error(state->service));
    lua_pushvalue(L, callback_index);
    LuaLlamaRequest pending;
    pending.type = LuaLlamaRequestType::Load;
    pending.callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    state->requests.emplace(request, std::move(pending));
    return 0;
}

static const luaL_Reg llamacpp_funcs[] = {
    {"isAvailable", l_available},
    {"getDevices", l_devices},
    {"loadModel", l_load},
    {"getError", l_error},
    {NULL, NULL},
};

static void on_event(const LlamaCppEvent *event, void *opaque)
{
    auto *state = static_cast<LuaLlamaCppContext *>(opaque);
    auto found = state->requests.find(event->request_id);
    if (found == state->requests.end())
        return;
    LuaLlamaRequest &pending = found->second;
    lua_State *L = state->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, pending.callback_ref);
    if (pending.type == LuaLlamaRequestType::Load)
    {
        if (event->type == LLAMACPP_EVENT_MODEL_LOADED)
        {
            state->models[event->model] = event->model_info;
            push_model(L, state, event->model);
            lua_pushnil(L);
        }
        else
        {
            lua_pushnil(L);
            lua_pushlstring(L, event->text, event->text_length);
        }
        if (lua_pcall(L, 2, 0, 0) != LUA_OK)
            lua_pop(L, 1);
    }
    else if (event->type == LLAMACPP_EVENT_TEXT)
    {
        pending.generated_text.append(event->text, event->text_length);
        lua_getfield(L, -1, "onText");
        if (lua_isfunction(L, -1))
        {
            lua_pushlstring(L, event->text, event->text_length);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                lua_pop(L, 1);
        }
        else
        {
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return;
    }
    else
    {
        const char *name = event->type == LLAMACPP_EVENT_COMPLETE
                               ? "onComplete"
                               : "onError";
        lua_getfield(L, -1, name);
        if (lua_isfunction(L, -1))
        {
            if (event->type == LLAMACPP_EVENT_COMPLETE)
            {
                lua_createtable(L, 0, 6);
                lua_pushstring(L, event->cancelled ? "cancelled" : event->stopped ? "stop"
                                                                                  : "length");
                lua_setfield(L, -2, "finishReason");
                lua_pushlstring(L, pending.generated_text.data(),
                                pending.generated_text.size());
                lua_setfield(L, -2, "text");
                lua_pushinteger(L, event->prompt_tokens);
                lua_setfield(L, -2, "promptTokens");
                lua_pushinteger(L, event->generated_tokens);
                lua_setfield(L, -2, "generatedTokens");
                lua_pushnumber(L, event->prompt_tokens_per_second);
                lua_setfield(L, -2, "promptTokensPerSecond");
                lua_pushnumber(L, event->generated_tokens_per_second);
                lua_setfield(L, -2, "generatedTokensPerSecond");
                state->chat_messages[pending.chat].emplace_back(
                    pending.generated_text);
            }
            else
            {
                lua_createtable(L, 0, 1);
                lua_pushlstring(L, event->text, event->text_length);
                lua_setfield(L, -2, "message");
            }
            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                lua_pop(L, 1);
        }
        else
        {
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, pending.callback_ref);
    state->requests.erase(found);
}

LuaLlamaCppContext *lua_llamacpp_init(void *opaque, FileContext *files)
{
    auto *L = static_cast<lua_State *>(opaque);
    auto *state = new LuaLlamaCppContext();
    state->L = L;
    state->service = llamacpp_service_create(files);
    if (!state->service)
    {
        delete state;
        return nullptr;
    }
    llamacpp_service_set_event_callback(state->service, on_event, state);
    lua_getglobal(L, "sys");
    lua_newtable(L);
    lua_pushlightuserdata(L, state);
    luaL_setfuncs(L, llamacpp_funcs, 1);
    lua_setfield(L, -2, "llamacpp");
    lua_pop(L, 1);
    return state;
}

void lua_llamacpp_poll(LuaLlamaCppContext *state)
{
    if (state)
        llamacpp_service_poll(state->service);
}

void lua_llamacpp_cancel_all(LuaLlamaCppContext *state)
{
    if (state)
        llamacpp_service_cancel_all(state->service);
}

void lua_llamacpp_cleanup(LuaLlamaCppContext *state)
{
    if (!state)
        return;
    for (auto &item : state->requests)
        luaL_unref(state->L, LUA_REGISTRYINDEX, item.second.callback_ref);
    state->requests.clear();
    llamacpp_service_set_event_callback(state->service, nullptr, nullptr);
    llamacpp_service_destroy(state->service);
    delete state;
}
