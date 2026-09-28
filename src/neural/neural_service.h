#ifndef BUDO_NEURAL_SERVICE_H
#define BUDO_NEURAL_SERVICE_H

#include "core/api_error.h"
#include "neural_wrapper.h"

bool neural_service_is_available(ApiError *error);
int neural_service_load_model(NeuralContext *context, const char *path,
                              ApiError *error);
int neural_service_load_model_from_buffer(NeuralContext *context,
                                          const void *data, size_t size,
                                          ApiError *error);
bool neural_service_get_model_info(NeuralContext *context, int model_id,
                                   NeuralModelInfo *info, ApiError *error);
bool neural_service_run(NeuralContext *context, int model_id,
                        const NeuralTensor *inputs, int input_count,
                        NeuralTensor *outputs, int output_count,
                        ApiError *error);

#endif