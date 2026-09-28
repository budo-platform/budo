#ifndef TESTS_AUDIO_MOCK_H
#define TESTS_AUDIO_MOCK_H

#include "audio/audio_wrapper.h"

void audio_mock_reset(void);
int audio_mock_create_count(void);
int audio_mock_destroy_count(void);
int audio_mock_live_count(void);
int audio_mock_asset_root_id(const char *asset_root);

#endif