#ifndef NEURAL_WRAPPER_H
#define NEURAL_WRAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NEURAL_MAX_MODELS 8

#define NEURAL_MAX_RANK 8

#define NEURAL_MAX_TENSORS 32

    typedef enum
    {
        NEURAL_DTYPE_FLOAT32 = 0,
        NEURAL_DTYPE_INT32 = 1,
        NEURAL_DTYPE_INT64 = 2,
        NEURAL_DTYPE_UINT8 = 3,
        NEURAL_DTYPE_UNKNOWN = 99
    } NeuralDtype;

    typedef struct
    {
        char name[128];
        int rank;
        int64_t shape[NEURAL_MAX_RANK]; 
        NeuralDtype dtype;
    } NeuralTensorInfo;

    typedef struct
    {
        char description[512];
        char domain[128];
        char graph_name[128];
        char producer_name[128];
        int64_t version;
        int input_count;
        int output_count;
        NeuralTensorInfo inputs[NEURAL_MAX_TENSORS];
        NeuralTensorInfo outputs[NEURAL_MAX_TENSORS];
    } NeuralModelInfo;

    typedef struct
    {
        const void *data;
        size_t byte_count;
        int64_t shape[NEURAL_MAX_RANK];
        int rank;
        NeuralDtype dtype;
    } NeuralTensor;

    typedef struct NeuralContext NeuralContext;

    bool neural_is_available(void);

    NeuralContext *neural_create(const char *project_dir);

    void neural_destroy(NeuralContext *ctx);

    int neural_load_model(NeuralContext *ctx, const char *rel_path);

    int neural_load_model_from_buffer(NeuralContext *ctx, const void *data, size_t size);

    void neural_unload_model(NeuralContext *ctx, int model_id);

    bool neural_get_model_info(NeuralContext *ctx, int model_id,
                               NeuralModelInfo *out);

    bool neural_run(NeuralContext *ctx, int model_id,
                    const NeuralTensor *inputs, int n_inputs,
                    NeuralTensor *outputs, int n_outputs);

    const char *neural_get_error(NeuralContext *ctx);

#ifdef __cplusplus
}
#endif

#endif