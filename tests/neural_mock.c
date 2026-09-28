#include "tests/neural_mock.h"

#include <stdlib.h>
#include <string.h>

struct NeuralContext
{
    char project_dir[128];
    bool model_loaded;
    float output;
};

float neural_mock_project_bias(const char *project_dir)
{
    size_t length = project_dir ? strlen(project_dir) : 0;
    return length > 0 ? (float)(unsigned char)project_dir[length - 1] : 0.0f;
}

bool neural_is_available(void)
{
    return true;
}

NeuralContext *neural_create(const char *project_dir)
{
    NeuralContext *ctx = (NeuralContext *)calloc(1, sizeof(NeuralContext));
    if (!ctx)
        return NULL;

    if (project_dir)
    {
        strncpy(ctx->project_dir, project_dir, sizeof(ctx->project_dir) - 1);
        ctx->project_dir[sizeof(ctx->project_dir) - 1] = '\0';
    }
    return ctx;
}

void neural_destroy(NeuralContext *ctx)
{
    free(ctx);
}

int neural_load_model(NeuralContext *ctx, const char *rel_path)
{
    if (!ctx || !rel_path)
        return -1;
    ctx->model_loaded = true;
    return 0;
}

int neural_load_model_from_buffer(NeuralContext *ctx, const void *data, size_t size)
{
    if (!ctx || !data || size == 0)
        return -1;
    ctx->model_loaded = true;
    return 0;
}

void neural_unload_model(NeuralContext *ctx, int model_id)
{
    if (ctx && model_id == 0)
        ctx->model_loaded = false;
}

bool neural_get_model_info(NeuralContext *ctx, int model_id,
                           NeuralModelInfo *out)
{
    if (!ctx || !ctx->model_loaded || model_id != 0 || !out)
        return false;

    memset(out, 0, sizeof(*out));
    out->input_count = 1;
    out->output_count = 1;
    strcpy(out->inputs[0].name, "input");
    out->inputs[0].rank = 1;
    out->inputs[0].shape[0] = 1;
    out->inputs[0].dtype = NEURAL_DTYPE_FLOAT32;
    strcpy(out->outputs[0].name, "output");
    out->outputs[0].rank = 1;
    out->outputs[0].shape[0] = 1;
    out->outputs[0].dtype = NEURAL_DTYPE_FLOAT32;
    return true;
}

bool neural_run(NeuralContext *ctx, int model_id,
                const NeuralTensor *inputs, int n_inputs,
                NeuralTensor *outputs, int n_outputs)
{
    if (!ctx || !ctx->model_loaded || model_id != 0 ||
        !inputs || n_inputs != 1 || !inputs[0].data ||
        inputs[0].byte_count < sizeof(float) || !outputs || n_outputs != 1)
        return false;

    ctx->output = *(const float *)inputs[0].data +
                  neural_mock_project_bias(ctx->project_dir);
    memset(&outputs[0], 0, sizeof(outputs[0]));
    outputs[0].data = &ctx->output;
    outputs[0].byte_count = sizeof(ctx->output);
    outputs[0].shape[0] = 1;
    outputs[0].rank = 1;
    outputs[0].dtype = NEURAL_DTYPE_FLOAT32;
    return true;
}

const char *neural_get_error(NeuralContext *ctx)
{
    return ctx ? ctx->project_dir : "";
}