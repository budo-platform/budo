#ifndef TESTS_AUDIO_MOCK_H
#define TESTS_AUDIO_MOCK_H

#include "audio/audio_wrapper.h"

void audio_mock_reset(void);
int audio_mock_create_count(void);
int audio_mock_destroy_count(void);
int audio_mock_live_count(void);
int audio_mock_asset_root_id(const char *asset_root);

AudioContext *audio_mock_last_context(void);

void audio_mock_mix(AudioContext *ctx, float *out, int frames);
void audio_mock_capture(AudioContext *ctx, const float *in, int frames, int channels);

#endif