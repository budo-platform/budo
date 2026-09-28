#ifndef BUDO_AUDIO_SERVICE_H
#define BUDO_AUDIO_SERVICE_H

#include "audio_wrapper.h"
#include "core/api_error.h"

int audio_service_create_oscillator(AudioContext *context, ApiError *error);
int audio_service_load_buffer(AudioContext *context, const char *path,
                              ApiError *error);

#endif