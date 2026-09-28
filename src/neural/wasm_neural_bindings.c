#include "wasm_util/wasm_memory.h"
#include "wasm_neural_bindings.h"
#include "neural_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    bool valid;
    char name[128];
    void *data; 
    size_t byte_count;
    int64_t shape[NEURAL_MAX_RANK];
    int rank;
} WasmStagedInput;

typedef struct
{
    WasmStagedInput staged[NEURAL_MAX_TENSORS];
    NeuralTensor outputs[NEURAL_MAX_TENSORS];
    NeuralModelInfo model_info;
    bool model_info_valid;
    int n_outputs;
    bool ran;
} WasmModelState;

struct WasmNeuralBindingState
{
    NeuralContext *neural_ctx;
    wasmtime_context_t *store_ctx;
    wasmtime_memory_t memory;
    bool memory_valid;
    WasmModelState model_states[NEURAL_MAX_MODELS];
};

static void staged_inputs_clear(WasmNeuralBindingState *state, int model_id)
{
    if (!state || model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return;
    WasmModelState *s = &state->model_states[model_id];
    for (int i = 0; i < NEURAL_MAX_TENSORS; i++)
    {
        if (s->staged[i].valid && s->staged[i].data)
        {
            free(s->staged[i].data);
            s->staged[i].data = NULL;
        }
        s->staged[i].valid = false;
    }
}

static bool mem_read_str(WasmNeuralBindingState *state,
                         int32_t ptr, int32_t len,
                         char *buf, size_t buf_size)
{
    if (!state || !state->memory_valid || !state->store_ctx)
        return false;
    if (ptr < 0 || len < 0 || (size_t)len + 1 > buf_size)
        return false;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

    if ((size_t)ptr + (size_t)len > mem_size)
        return false;

    memcpy(buf, mem + ptr, (size_t)len);
    buf[len] = '\0';
    return true;
}

static int32_t mem_write_str(WasmNeuralBindingState *state,
                             int32_t ptr, int32_t max_len, const char *str)
{
    if (!state || !state->memory_valid || !state->store_ctx)
        return 0;
    if (ptr < 0 || max_len <= 0 || !str)
        return 0;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

    int32_t str_len = (int32_t)strlen(str);
    int32_t write_len = str_len < max_len - 1 ? str_len : max_len - 1;

    if ((size_t)ptr + (size_t)write_len + 1 > mem_size)
        return 0;

    memcpy(mem + ptr, str, (size_t)write_len);
    mem[ptr + write_len] = '\0';
    return write_len;
}

static bool mem_read_bytes(WasmNeuralBindingState *state,
                           int32_t ptr, size_t byte_count, void *out)
{
    if (!state || !state->memory_valid || !state->store_ctx)
        return false;
    if (ptr < 0 || byte_count == 0 || !out)
        return false;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

    if ((size_t)ptr + byte_count > mem_size)
        return false;

    memcpy(out, mem + ptr, byte_count);
    return true;
}

static bool mem_write_bytes(WasmNeuralBindingState *state,
                            int32_t ptr, const void *data,
                            size_t byte_count, size_t max_bytes)
{
    if (!state || !state->memory_valid || !state->store_ctx)
        return false;
    if (ptr < 0 || !data || byte_count == 0)
        return false;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

    size_t write = byte_count < max_bytes ? byte_count : max_bytes;
    if ((size_t)ptr + write > mem_size)
        return false;

    memcpy(mem + ptr, data, write);
    return true;
}

static bool mem_read_i64_array(WasmNeuralBindingState *state,
                               int32_t ptr, int count,
                               int64_t *out, int max_count)
{
    if (!state || !state->memory_valid || !state->store_ctx)
        return false;
    if (ptr < 0 || count <= 0 || count > max_count || !out)
        return false;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

    if ((size_t)ptr + (size_t)count * 8 > mem_size)
        return false;

    memcpy(out, mem + ptr, (size_t)count * 8);
    return true;
}

static int mem_write_i64_array(WasmNeuralBindingState *state,
                               int32_t ptr, const int64_t *data,
                               int count, int max_count)
{
    if (!state || !state->memory_valid || !state->store_ctx)
        return 0;
    if (ptr < 0 || count <= 0 || !data)
        return 0;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);

    int write_count = count < max_count ? count : max_count;
    if ((size_t)ptr + (size_t)write_count * 8 > mem_size)
        return 0;

    memcpy(mem + ptr, data, (size_t)write_count * 8);
    return write_count;
}

static bool ensure_model_info(WasmNeuralBindingState *state, int model_id)
{
    if (!state || !state->neural_ctx ||
        model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return false;

    WasmModelState *s = &state->model_states[model_id];
    if (s->model_info_valid)
        return true;

    s->model_info_valid = neural_get_model_info(state->neural_ctx,
                                                model_id, &s->model_info);
    return s->model_info_valid;
}

static wasmtime_error_t *define_neural_func(
    wasmtime_linker_t *linker,
    WasmNeuralBindingState *state,
    const char *name,
    wasmtime_func_callback_t callback,
    const wasm_valkind_t *param_kinds, size_t n_params,
    const wasm_valkind_t *result_kinds, size_t n_results)
{
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, n_params);
    for (size_t index = 0; index < n_params; index++)
        params.data[index] = wasm_valtype_new(param_kinds[index]);
    wasm_valtype_vec_new_uninitialized(&results, n_results);
    for (size_t index = 0; index < n_results; index++)
        results.data[index] = wasm_valtype_new(result_kinds[index]);

    wasm_functype_t *functype = wasm_functype_new(&params, &results);
    wasmtime_error_t *error = wasmtime_linker_define_func(
        linker, "env", 3, name, strlen(name), functype, callback, state, NULL);
    wasm_functype_delete(functype);
    return error;
}

static wasm_trap_t *host_neural_is_available(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = neural_is_available() ? 1 : 0;
    return NULL;
}

static wasm_trap_t *host_neural_get_error(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 2)
        return NULL;

    const char *err = neural_get_error(state->neural_ctx);
    results[0].of.i32 = mem_write_str(state, args[0].of.i32, args[1].of.i32,
                                      err ? err : "");
    return NULL;
}

static wasm_trap_t *host_neural_load_model(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->neural_ctx || nargs < 2)
        return NULL;

    char path[512];
    if (!mem_read_str(state, args[0].of.i32, args[1].of.i32,
                      path, sizeof(path)))
        return NULL;

    ApiError error;
    int model_id = neural_service_load_model(state->neural_ctx, path, &error);
    results[0].of.i32 = model_id;

    if (model_id >= 0)
        ensure_model_info(state, model_id);

    return NULL;
}

static wasm_trap_t *host_neural_load_model_from_buffer(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;

    if (!state || !state->neural_ctx || !state->memory_valid ||
        !state->store_ctx || nargs < 2)
        return NULL;
    if (args[0].of.i32 < 0 || args[1].of.i32 <= 0)
        return NULL;

    uint8_t *mem = wasmtime_memory_data(state->store_ctx, &state->memory);
    size_t mem_size = wasmtime_memory_data_size(state->store_ctx, &state->memory);
    size_t ptr = (size_t)args[0].of.i32;
    size_t len = (size_t)args[1].of.i32;
    if (ptr + len > mem_size)
        return NULL;

    int model_id = neural_load_model_from_buffer(state->neural_ctx, mem + ptr, len);
    results[0].of.i32 = model_id;

    if (model_id >= 0)
        ensure_model_info(state, model_id);

    return NULL;
}

static wasm_trap_t *host_neural_unload_model(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)results;
    (void)nresults;
    if (!state || !state->neural_ctx || nargs < 1)
        return NULL;

    int32_t model_id = args[0].of.i32;
    if (model_id >= 0 && model_id < NEURAL_MAX_MODELS)
    {
        staged_inputs_clear(state, model_id);
        memset(&state->model_states[model_id], 0, sizeof(WasmModelState));
    }
    neural_unload_model(state->neural_ctx, model_id);
    return NULL;
}

static wasm_trap_t *host_neural_get_input_count(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 1)
        return NULL;

    int32_t model_id = args[0].of.i32;
    if (!ensure_model_info(state, model_id))
        return NULL;

    results[0].of.i32 = state->model_states[model_id].model_info.input_count;
    return NULL;
}

static wasm_trap_t *host_neural_get_output_count(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 1)
        return NULL;

    int32_t model_id = args[0].of.i32;
    if (!ensure_model_info(state, model_id))
        return NULL;

    results[0].of.i32 = state->model_states[model_id].model_info.output_count;
    return NULL;
}

static wasm_trap_t *host_neural_get_input_info(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 5)
        return NULL;

    int32_t model_id = args[0].of.i32;
    int32_t index = args[1].of.i32;
    int32_t name_dst = args[2].of.i32;
    int32_t name_max_len = args[3].of.i32;
    int32_t shape_dst = args[4].of.i32;

    if (!ensure_model_info(state, model_id))
        return NULL;

    NeuralModelInfo *info = &state->model_states[model_id].model_info;
    if (index < 0 || index >= info->input_count)
        return NULL;

    NeuralTensorInfo *ti = &info->inputs[index];
    mem_write_str(state, name_dst, name_max_len, ti->name);
    mem_write_i64_array(state, shape_dst, ti->shape, ti->rank, NEURAL_MAX_RANK);
    results[0].of.i32 = ti->rank;
    return NULL;
}

static wasm_trap_t *host_neural_get_output_info(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 5)
        return NULL;

    int32_t model_id = args[0].of.i32;
    int32_t index = args[1].of.i32;
    int32_t name_dst = args[2].of.i32;
    int32_t name_max_len = args[3].of.i32;
    int32_t shape_dst = args[4].of.i32;

    if (!ensure_model_info(state, model_id))
        return NULL;

    NeuralModelInfo *info = &state->model_states[model_id].model_info;
    if (index < 0 || index >= info->output_count)
        return NULL;

    NeuralTensorInfo *ti = &info->outputs[index];
    mem_write_str(state, name_dst, name_max_len, ti->name);
    mem_write_i64_array(state, shape_dst, ti->shape, ti->rank, NEURAL_MAX_RANK);
    results[0].of.i32 = ti->rank;
    return NULL;
}

static wasm_trap_t *host_neural_set_input_f32(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)results;
    (void)nresults;

    if (!state || !state->neural_ctx || nargs < 7)
        return NULL;

    int32_t model_id = args[0].of.i32;
    int32_t name_ptr = args[1].of.i32;
    int32_t name_len = args[2].of.i32;
    int32_t data_ptr = args[3].of.i32;
    int32_t elem_count = args[4].of.i32;
    int32_t shape_ptr = args[5].of.i32;
    int32_t rank = args[6].of.i32;

    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS || elem_count <= 0)
        return NULL;

    char name[128];
    if (!mem_read_str(state, name_ptr, name_len, name, sizeof(name)))
        return NULL;

    size_t byte_count = (size_t)elem_count * sizeof(float);
    void *data_copy = malloc(byte_count);
    if (!data_copy)
        return NULL;

    if (!mem_read_bytes(state, data_ptr, byte_count, data_copy))
    {
        free(data_copy);
        return NULL;
    }

    int64_t shape[NEURAL_MAX_RANK] = {0};
    int actual_rank = 0;
    if (rank > 0 && shape_ptr != 0 && rank <= NEURAL_MAX_RANK)
    {
        if (mem_read_i64_array(state, shape_ptr, rank, shape, NEURAL_MAX_RANK))
            actual_rank = rank;
    }

    WasmModelState *model_state = &state->model_states[model_id];
    int slot = -1;
    for (int i = 0; i < NEURAL_MAX_TENSORS; i++)
    {
        if (model_state->staged[i].valid &&
            strcmp(model_state->staged[i].name, name) == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        for (int i = 0; i < NEURAL_MAX_TENSORS; i++)
        {
            if (!model_state->staged[i].valid)
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
    {
        
        free(data_copy);
        return NULL;
    }

    if (model_state->staged[slot].valid && model_state->staged[slot].data)
        free(model_state->staged[slot].data);

    WasmStagedInput *s = &model_state->staged[slot];
    s->valid = true;
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->data = data_copy;
    s->byte_count = byte_count;
    memcpy(s->shape, shape, sizeof(shape));
    s->rank = actual_rank;

    return NULL;
}

static wasm_trap_t *host_neural_run(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 1)
        return NULL;

    int32_t model_id = args[0].of.i32;
    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return NULL;

    WasmModelState *model_state = &state->model_states[model_id];

    if (!ensure_model_info(state, model_id))
        return NULL;

    NeuralModelInfo *info = &model_state->model_info;
    int n_inputs = info->input_count;
    int n_outputs = info->output_count;

    NeuralTensor inputs[NEURAL_MAX_TENSORS];
    memset(inputs, 0, sizeof(inputs));

    for (int i = 0; i < n_inputs; i++)
    {
        const char *want = info->inputs[i].name;
        NeuralTensorInfo *ti = &info->inputs[i];

        WasmStagedInput *found = NULL;
        for (int j = 0; j < NEURAL_MAX_TENSORS; j++)
        {
            if (model_state->staged[j].valid &&
                strcmp(model_state->staged[j].name, want) == 0)
            {
                found = &model_state->staged[j];
                break;
            }
        }

        if (!found)
        {
            fprintf(stderr, "neural_run: missing staged input '%s' for model %d\n",
                    want, model_id);
            staged_inputs_clear(state, model_id);
            return NULL;
        }

        inputs[i].data = found->data;
        inputs[i].byte_count = found->byte_count;
        inputs[i].dtype = NEURAL_DTYPE_FLOAT32;

        if (found->rank > 0)
        {
            inputs[i].rank = found->rank;
            memcpy(inputs[i].shape, found->shape,
                   (size_t)found->rank * sizeof(int64_t));
        }
        else
        {
            inputs[i].rank = ti->rank;
            memcpy(inputs[i].shape, ti->shape,
                   (size_t)ti->rank * sizeof(int64_t));
        }
    }

    NeuralTensor outputs[NEURAL_MAX_TENSORS];
    memset(outputs, 0, sizeof(outputs));

    bool ok = neural_run(state->neural_ctx, model_id,
                         inputs, n_inputs, outputs, n_outputs);

    staged_inputs_clear(state, model_id);

    if (ok)
    {

        memcpy(model_state->outputs, outputs,
               (size_t)n_outputs * sizeof(NeuralTensor));
        model_state->n_outputs = n_outputs;
        model_state->ran = true;
        results[0].of.i32 = 1;
    }

    return NULL;
}

static wasm_trap_t *host_neural_copy_output_f32(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 5)
        return NULL;

    int32_t model_id = args[0].of.i32;
    int32_t name_ptr = args[1].of.i32;
    int32_t name_len = args[2].of.i32;
    int32_t dest_ptr = args[3].of.i32;
    int32_t max_elems = args[4].of.i32;

    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return NULL;

    WasmModelState *model_state = &state->model_states[model_id];
    if (!model_state->ran || !model_state->model_info_valid)
        return NULL;

    char name[128];
    if (!mem_read_str(state, name_ptr, name_len, name, sizeof(name)))
        return NULL;

    int out_idx = -1;
    for (int i = 0; i < model_state->model_info.output_count; i++)
    {
        if (strcmp(model_state->model_info.outputs[i].name, name) == 0)
        {
            out_idx = i;
            break;
        }
    }
    if (out_idx < 0 || out_idx >= model_state->n_outputs)
        return NULL;

    NeuralTensor *ot = &model_state->outputs[out_idx];
    if (!ot->data || ot->byte_count == 0)
        return NULL;

    int actual_elems = (int)(ot->byte_count / sizeof(float));
    int copy_elems = actual_elems < max_elems ? actual_elems : max_elems;

    if (!mem_write_bytes(state, dest_ptr, ot->data,
                         (size_t)copy_elems * sizeof(float),
                         (size_t)max_elems * sizeof(float)))
        return NULL;

    results[0].of.i32 = copy_elems;
    return NULL;
}

static wasm_trap_t *host_neural_get_output_shape(
    void *env, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    WasmNeuralBindingState *state = (WasmNeuralBindingState *)env;
    (void)caller;
    (void)nresults;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = 0;

    if (!state || !state->neural_ctx || nargs < 5)
        return NULL;

    int32_t model_id = args[0].of.i32;
    int32_t name_ptr = args[1].of.i32;
    int32_t name_len = args[2].of.i32;
    int32_t dest_ptr = args[3].of.i32;
    int32_t max_rank = args[4].of.i32;

    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return NULL;

    WasmModelState *model_state = &state->model_states[model_id];
    if (!model_state->ran || !model_state->model_info_valid)
        return NULL;

    char name[128];
    if (!mem_read_str(state, name_ptr, name_len, name, sizeof(name)))
        return NULL;

    int out_idx = -1;
    for (int i = 0; i < model_state->model_info.output_count; i++)
    {
        if (strcmp(model_state->model_info.outputs[i].name, name) == 0)
        {
            out_idx = i;
            break;
        }
    }
    if (out_idx < 0 || out_idx >= model_state->n_outputs)
        return NULL;

    NeuralTensor *ot = &model_state->outputs[out_idx];
    int written = mem_write_i64_array(state, dest_ptr, ot->shape,
                                      ot->rank, max_rank);
    if (written <= 0 && ot->rank > 0)
        return NULL;

    results[0].of.i32 = ot->rank;
    return NULL;
}

wasmtime_error_t *wasm_neural_register(wasmtime_linker_t *linker,
                                       WasmNeuralBindingState *state)
{
    wasmtime_error_t *error;
    wasm_valkind_t no_params[1] = {0};
    wasm_valkind_t i32_result[1] = {WASM_I32};

    error = define_neural_func(linker, state, "neural_is_available",
                               host_neural_is_available,
                               no_params, 0, i32_result, 1);
    if (error)
        return error;

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_get_error",
                                   host_neural_get_error,
                                   p, 2, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_load_model",
                                   host_neural_load_model,
                                   p, 2, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_load_model_from_buffer",
                                   host_neural_load_model_from_buffer,
                                   p, 2, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32};
        error = define_neural_func(linker, state, "neural_unload_model",
                                   host_neural_unload_model,
                                   p, 1, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32};
        error = define_neural_func(linker, state, "neural_get_input_count",
                                   host_neural_get_input_count,
                                   p, 1, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32};
        error = define_neural_func(linker, state, "neural_get_output_count",
                                   host_neural_get_output_count,
                                   p, 1, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_get_input_info",
                                   host_neural_get_input_info,
                                   p, 5, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_get_output_info",
                                   host_neural_get_output_info,
                                   p, 5, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32, WASM_I32,
                              WASM_I32, WASM_I32, WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_set_input_f32",
                                   host_neural_set_input_f32,
                                   p, 7, NULL, 0);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32};
        error = define_neural_func(linker, state, "neural_run",
                                   host_neural_run,
                                   p, 1, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32, WASM_I32,
                              WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_copy_output_f32",
                                   host_neural_copy_output_f32,
                                   p, 5, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32, WASM_I32,
                              WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_get_output_f32",
                                   host_neural_copy_output_f32,
                                   p, 5, i32_result, 1);
        if (error)
            return error;
    }

    {
        wasm_valkind_t p[] = {WASM_I32, WASM_I32, WASM_I32,
                              WASM_I32, WASM_I32};
        error = define_neural_func(linker, state, "neural_get_output_shape",
                                   host_neural_get_output_shape,
                                   p, 5, i32_result, 1);
        if (error)
            return error;
    }

    return NULL;
}

WasmNeuralBindingState *wasm_neural_binding_state_create(void)
{
    return (WasmNeuralBindingState *)calloc(1, sizeof(WasmNeuralBindingState));
}

void wasm_neural_binding_state_destroy(WasmNeuralBindingState *state)
{
    if (!state)
        return;

    for (int i = 0; i < NEURAL_MAX_MODELS; i++)
        staged_inputs_clear(state, i);

    neural_destroy(state->neural_ctx);
    free(state);
}

bool wasm_neural_binding_state_enable(WasmNeuralBindingState *state,
                                      const char *project_dir)
{
    if (!state)
        return false;
    if (state->neural_ctx)
        return true;

    state->neural_ctx = neural_create(project_dir);
    return state->neural_ctx != NULL;
}

void wasm_neural_binding_state_set_memory(WasmNeuralBindingState *state,
                                          wasmtime_context_t *store_ctx,
                                          wasmtime_memory_t *memory)
{
    if (!state)
        return;

    state->store_ctx = store_ctx;
    if (memory)
    {
        state->memory = *memory;
        state->memory_valid = true;
    }
    else
    {
        state->memory_valid = false;
    }
}