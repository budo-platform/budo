#include "neural_service.h"

static bool neural_service_context(NeuralContext *context, ApiError *error)
{
    api_error_clear(error);
    if (context)
        return true;
    api_error_set(error, API_STATUS_UNSUPPORTED,
                  "neural.unavailable", "Neural inference is unavailable");
    return false;
}

static void neural_service_wrapper_error(NeuralContext *context,
                                         const char *code, ApiError *error)
{
    const char *message = neural_get_error(context);
    api_error_set(error, API_STATUS_APPLICATION_ERROR, code,
                  message && message[0] ? message : "Neural operation failed");
}

bool neural_service_is_available(ApiError *error)
{
    bool available;
    api_error_clear(error);
    available = neural_is_available();
    if (!available)
        api_error_set(error, API_STATUS_UNSUPPORTED,
                      "neural.unavailable", "Neural inference is unavailable");
    return available;
}

int neural_service_load_model(NeuralContext *context, const char *path,
                              ApiError *error)
{
    int result;
    if (!neural_service_context(context, error))
        return -1;
    if (!path || !path[0])
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "neural.invalid_path", "Model path is required");
        return -1;
    }
    result = neural_load_model(context, path);
    if (result < 0)
        neural_service_wrapper_error(context, "neural.load_failed", error);
    return result;
}

int neural_service_load_model_from_buffer(NeuralContext *context,
                                          const void *data, size_t size,
                                          ApiError *error)
{
    int result;
    if (!neural_service_context(context, error))
        return -1;
    if (!data || size == 0)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "neural.invalid_buffer", "Model data is required");
        return -1;
    }
    result = neural_load_model_from_buffer(context, data, size);
    if (result < 0)
        neural_service_wrapper_error(context, "neural.load_failed", error);
    return result;
}

bool neural_service_get_model_info(NeuralContext *context, int model_id,
                                   NeuralModelInfo *info, ApiError *error)
{
    if (!neural_service_context(context, error))
        return false;
    if (model_id < 0 || !info)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "neural.invalid_model", "Model and output are required");
        return false;
    }
    if (neural_get_model_info(context, model_id, info))
        return true;
    neural_service_wrapper_error(context, "neural.info_failed", error);
    return false;
}

bool neural_service_run(NeuralContext *context, int model_id,
                        const NeuralTensor *inputs, int input_count,
                        NeuralTensor *outputs, int output_count,
                        ApiError *error)
{
    if (!neural_service_context(context, error))
        return false;
    if (model_id < 0 || !inputs || input_count < 0 || !outputs || output_count < 0)
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "neural.invalid_inference",
                      "Model, inputs, and outputs are required");
        return false;
    }
    if (neural_run(context, model_id, inputs, input_count, outputs, output_count))
        return true;
    neural_service_wrapper_error(context, "neural.run_failed", error);
    return false;
}