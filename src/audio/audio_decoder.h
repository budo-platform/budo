#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct AudioDecodedData
    {
        int sample_rate;
        int channels;
        int frame_count;
        float *samples;
    } AudioDecodedData;

    bool audio_decode_file(const char *path, AudioDecodedData *out, char *error, int error_size);
    bool audio_decode_memory(const uint8_t *data, size_t size, AudioDecodedData *out, char *error, int error_size);
    void audio_decoded_data_free(AudioDecodedData *data);

#ifdef __cplusplus
}
#endif

#endif