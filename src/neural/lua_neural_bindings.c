#include "lua_neural_bindings.h"
#include "neural_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

static NeuralContext *lua_neural_context(lua_State *L)
{
    return (NeuralContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static const char *dtype_to_string(NeuralDtype dtype)
{
    switch (dtype)
    {
    case NEURAL_DTYPE_FLOAT32:
        return "float32";
    case NEURAL_DTYPE_INT32:
        return "int32";
    case NEURAL_DTYPE_INT64:
        return "int64";
    case NEURAL_DTYPE_UINT8:
        return "uint8";
    default:
        return "unknown";
    }
}

static size_t dtype_byte_size(NeuralDtype dtype)
{
    switch (dtype)
    {
    case NEURAL_DTYPE_FLOAT32:
        return 4;
    case NEURAL_DTYPE_INT32:
        return 4;
    case NEURAL_DTYPE_INT64:
        return 8;
    case NEURAL_DTYPE_UINT8:
        return 1;
    default:
        return 4;
    }
}

static void push_tensor_info(lua_State *L, const NeuralTensorInfo *ti)
{
    lua_newtable(L);

    lua_pushstring(L, ti->name);
    lua_setfield(L, -2, "name");

    lua_pushstring(L, dtype_to_string(ti->dtype));
    lua_setfield(L, -2, "dtype");

    lua_newtable(L);
    for (int i = 0; i < ti->rank; i++)
    {
        lua_pushinteger(L, (lua_Integer)ti->shape[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "shape");
}

static int l_neural_is_available(lua_State *L)
{
    lua_pushboolean(L, neural_is_available());
    return 1;
}

static int l_neural_get_error(lua_State *L)
{
    NeuralContext *neural_ctx = lua_neural_context(L);
    if (!neural_ctx)
    {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, neural_get_error(neural_ctx));
    return 1;
}

static int l_neural_load_model(lua_State *L)
{
    NeuralContext *neural_ctx = lua_neural_context(L);
    if (!neural_ctx)
        return luaL_error(L, "neural: context not initialized");

    const char *path = luaL_checkstring(L, 1);
    ApiError error;
    int model_id = neural_service_load_model(neural_ctx, path, &error);
    if (model_id < 0)
        return luaL_error(L, "neural.loadModel: %s",
                          neural_get_error(neural_ctx));

    lua_pushinteger(L, (lua_Integer)model_id);
    return 1;
}

static int l_neural_load_model_from_buffer(lua_State *L)
{
    NeuralContext *neural_ctx = lua_neural_context(L);
    if (!neural_ctx)
        return luaL_error(L, "neural: context not initialized");

    size_t len = 0;
    const char *data = luaL_checklstring(L, 1, &len);
    int model_id = neural_load_model_from_buffer(neural_ctx, data, len);
    if (model_id < 0)
        return luaL_error(L, "neural.loadModelFromBuffer: %s",
                          neural_get_error(neural_ctx));

    lua_pushinteger(L, (lua_Integer)model_id);
    return 1;
}

static int l_neural_unload_model(lua_State *L)
{
    NeuralContext *neural_ctx = lua_neural_context(L);
    if (!neural_ctx)
        return 0;

    int model_id = (int)luaL_checkinteger(L, 1);
    neural_unload_model(neural_ctx, model_id);
    return 0;
}

static int l_neural_get_model_info(lua_State *L)
{
    NeuralContext *neural_ctx = lua_neural_context(L);
    if (!neural_ctx)
        return luaL_error(L, "neural: context not initialized");

    int model_id = (int)luaL_checkinteger(L, 1);

    NeuralModelInfo info;
    if (!neural_get_model_info(neural_ctx, model_id, &info))
        return luaL_error(L, "neural.getModelInfo: %s",
                          neural_get_error(neural_ctx));

    lua_newtable(L);

    lua_pushstring(L, info.description);
    lua_setfield(L, -2, "description");
    lua_pushstring(L, info.producer_name);
    lua_setfield(L, -2, "producerName");
    lua_pushstring(L, info.graph_name);
    lua_setfield(L, -2, "graphName");
    lua_pushstring(L, info.domain);
    lua_setfield(L, -2, "domain");
    lua_pushinteger(L, (lua_Integer)info.version);
    lua_setfield(L, -2, "version");

    lua_newtable(L);
    for (int i = 0; i < info.input_count; i++)
    {
        push_tensor_info(L, &info.inputs[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "inputs");

    lua_newtable(L);
    for (int i = 0; i < info.output_count; i++)
    {
        push_tensor_info(L, &info.outputs[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "outputs");

    return 1;
}

static int l_neural_run(lua_State *L)
{
    NeuralContext *neural_ctx = lua_neural_context(L);
    if (!neural_ctx)
        return luaL_error(L, "neural: context not initialized");

    int model_id = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    NeuralModelInfo info;
    if (!neural_get_model_info(neural_ctx, model_id, &info))
        return luaL_error(L, "neural.run: %s", neural_get_error(neural_ctx));

    void *input_bufs[NEURAL_MAX_TENSORS];
    memset(input_bufs, 0, sizeof(input_bufs));
    int n_alloc = 0; 

    NeuralTensor in_tensors[NEURAL_MAX_TENSORS];
    memset(in_tensors, 0, sizeof(in_tensors));

    for (int i = 0; i < info.input_count; i++)
    {
        const char *name = info.inputs[i].name;
        NeuralDtype dtype = info.inputs[i].dtype;

        lua_getfield(L, 2, name);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            for (int j = 0; j < n_alloc; j++)
                free(input_bufs[j]);
            return luaL_error(L, "neural.run: missing input tensor '%s'", name);
        }

        lua_getfield(L, -1, "data");
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 2); 
            for (int j = 0; j < n_alloc; j++)
                free(input_bufs[j]);
            return luaL_error(L,
                              "neural.run: input '%s'.data must be a table",
                              name);
        }

        int data_len = (int)lua_rawlen(L, -1);

        int64_t shape[NEURAL_MAX_RANK];
        int rank = info.inputs[i].rank;
        memcpy(shape, info.inputs[i].shape,
               sizeof(int64_t) * (size_t)rank);

        lua_getfield(L, -2, "shape");
        if (lua_istable(L, -1))
        {
            int provided_rank = (int)lua_rawlen(L, -1);
            
            if (provided_rank == rank)
            {
                for (int d = 0; d < rank; d++)
                {
                    lua_rawgeti(L, -1, d + 1);
                    shape[d] = (int64_t)lua_tointeger(L, -1);
                    lua_pop(L, 1);
                }
            }
        }
        lua_pop(L, 1); 

        int64_t static_prod = 1;
        int dynamic_idx = -1;
        for (int d = 0; d < rank; d++)
        {
            if (shape[d] < 0)
                dynamic_idx = d;
            else
                static_prod *= shape[d];
        }
        if (dynamic_idx >= 0 && static_prod > 0)
            shape[dynamic_idx] = (int64_t)(data_len / static_prod);

        size_t bpe = dtype_byte_size(dtype);
        size_t byte_count = (size_t)data_len * bpe;
        void *buf = malloc(byte_count > 0 ? byte_count : 1);
        if (!buf)
        {
            lua_pop(L, 2); 
            for (int j = 0; j < n_alloc; j++)
                free(input_bufs[j]);
            return luaL_error(L, "neural.run: out of memory for input '%s'",
                              name);
        }

        if (dtype == NEURAL_DTYPE_FLOAT32)
        {
            float *fbuf = (float *)buf;
            for (int k = 0; k < data_len; k++)
            {
                lua_rawgeti(L, -1, k + 1);
                fbuf[k] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
        else if (dtype == NEURAL_DTYPE_INT32)
        {
            int32_t *ibuf = (int32_t *)buf;
            for (int k = 0; k < data_len; k++)
            {
                lua_rawgeti(L, -1, k + 1);
                ibuf[k] = (int32_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
        }
        else if (dtype == NEURAL_DTYPE_INT64)
        {
            int64_t *ibuf = (int64_t *)buf;
            for (int k = 0; k < data_len; k++)
            {
                lua_rawgeti(L, -1, k + 1);
                ibuf[k] = (int64_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
        }
        else 
        {
            uint8_t *ubuf = (uint8_t *)buf;
            for (int k = 0; k < data_len; k++)
            {
                lua_rawgeti(L, -1, k + 1);
                ubuf[k] = (uint8_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
        }

        lua_pop(L, 2); 

        in_tensors[i].data = buf;
        in_tensors[i].byte_count = byte_count;
        in_tensors[i].rank = rank;
        in_tensors[i].dtype = dtype;
        memcpy(in_tensors[i].shape, shape,
               sizeof(int64_t) * (size_t)rank);

        input_bufs[n_alloc++] = buf;
    }

    NeuralTensor out_tensors[NEURAL_MAX_TENSORS];
    memset(out_tensors, 0, sizeof(out_tensors));

    bool ok = neural_run(neural_ctx, model_id,
                         in_tensors, info.input_count,
                         out_tensors, info.output_count);

    for (int i = 0; i < n_alloc; i++)
        free(input_bufs[i]);

    if (!ok)
        return luaL_error(L, "neural.run: %s",
                          neural_get_error(neural_ctx));

    lua_newtable(L); 

    for (int i = 0; i < info.output_count; i++)
    {
        const NeuralTensor *t = &out_tensors[i];
        const char *oname = info.outputs[i].name;
        size_t bpe = dtype_byte_size(t->dtype);
        size_t elem_count = (bpe > 0 && t->byte_count > 0)
                                ? t->byte_count / bpe
                                : 0;

        lua_newtable(L); 

        lua_newtable(L);
        if (t->dtype == NEURAL_DTYPE_FLOAT32)
        {
            const float *fbuf = (const float *)t->data;
            for (size_t k = 0; k < elem_count; k++)
            {
                lua_pushnumber(L, (lua_Number)fbuf[k]);
                lua_rawseti(L, -2, (lua_Integer)(k + 1));
            }
        }
        else if (t->dtype == NEURAL_DTYPE_INT32)
        {
            const int32_t *ibuf = (const int32_t *)t->data;
            for (size_t k = 0; k < elem_count; k++)
            {
                lua_pushinteger(L, (lua_Integer)ibuf[k]);
                lua_rawseti(L, -2, (lua_Integer)(k + 1));
            }
        }
        else if (t->dtype == NEURAL_DTYPE_INT64)
        {
            const int64_t *ibuf = (const int64_t *)t->data;
            for (size_t k = 0; k < elem_count; k++)
            {
                lua_pushinteger(L, (lua_Integer)ibuf[k]);
                lua_rawseti(L, -2, (lua_Integer)(k + 1));
            }
        }
        else 
        {
            const uint8_t *ubuf = (const uint8_t *)t->data;
            for (size_t k = 0; k < elem_count; k++)
            {
                lua_pushinteger(L, (lua_Integer)ubuf[k]);
                lua_rawseti(L, -2, (lua_Integer)(k + 1));
            }
        }
        lua_setfield(L, -2, "data");

        lua_newtable(L);
        for (int d = 0; d < t->rank; d++)
        {
            lua_pushinteger(L, (lua_Integer)t->shape[d]);
            lua_rawseti(L, -2, d + 1);
        }
        lua_setfield(L, -2, "shape");

        lua_pushstring(L, dtype_to_string(t->dtype));
        lua_setfield(L, -2, "dtype");

        lua_setfield(L, -2, oname); 
    }

    return 1; 
}

static const luaL_Reg neural_funcs[] = {
    {"isAvailable", l_neural_is_available},
    {"getError", l_neural_get_error},
    {"loadModel", l_neural_load_model},
    {"loadModelFromBuffer", l_neural_load_model_from_buffer},
    {"unloadModel", l_neural_unload_model},
    {"getModelInfo", l_neural_get_model_info},
    {"run", l_neural_run},
    {NULL, NULL}};

void lua_neural_init(void *L_void, NeuralContext *neural_ctx)
{
    lua_State *L = (lua_State *)L_void;

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, neural_ctx);
    luaL_setfuncs(L, neural_funcs, 1);
    lua_setfield(L, -2, "neural");

    lua_pop(L, 1); 

    static const char k_neural_staged_shim[] =
        "local n = sys.neural\n"
        "if n then\n"
        "  n._stagedInputs = {}\n"
        "  n._stagedOutputs = {}\n"
        "  function n.setInput(modelId, name, data, shape)\n"
        "    local m = n._stagedInputs[modelId]\n"
        "    if not m then m = {}; n._stagedInputs[modelId] = m end\n"
        "    if shape then\n"
        "      m[name] = { data = data, shape = shape }\n"
        "    else\n"
        "      m[name] = { data = data }\n"
        "    end\n"
        "  end\n"
        "  function n.getOutput(modelId, name)\n"
        "    local o = n._stagedOutputs[modelId]\n"
        "    if not o then return nil end\n"
        "    return o[name]\n"
        "  end\n"
        "  local _origRun = n.run\n"
        "  function n.run(modelId, inputs)\n"
        "    if inputs == nil then inputs = n._stagedInputs[modelId] or {} end\n"
        "    local outputs = _origRun(modelId, inputs)\n"
        "    n._stagedOutputs[modelId] = outputs\n"
        "    return outputs\n"
        "  end\n"
        "  local _origUnload = n.unloadModel\n"
        "  function n.unloadModel(modelId)\n"
        "    n._stagedInputs[modelId] = nil\n"
        "    n._stagedOutputs[modelId] = nil\n"
        "    return _origUnload(modelId)\n"
        "  end\n"
        "end\n";
    if (luaL_dostring(L, k_neural_staged_shim) != LUA_OK)
    {
        const char *msg = lua_tostring(L, -1);
        fprintf(stderr, "[neural] failed to install staged shim: %s\n",
                msg ? msg : "<unknown>");
        lua_pop(L, 1);
    }
}