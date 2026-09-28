#include "audio_decoder.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MA_NO_DEVICE_IO
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static bool audio_decode_from_decoder(ma_decoder *decoder, const char *label,
                                      AudioDecodedData *out, char *error, int error_size)
{
    ma_result result;
    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0;
    ma_uint32 sample_rate = 0;
    ma_uint64 frame_count = 0;
    float *pcm = NULL;
    ma_uint64 frames_read = 0;

    result = ma_decoder_get_data_format(decoder, &format, &channels, &sample_rate, NULL, 0);
    if (result != MA_SUCCESS || format != ma_format_f32 || channels == 0 || channels > 2 || sample_rate == 0)
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: unsupported decoded format: %s", label);
        return false;
    }

    result = ma_decoder_get_length_in_pcm_frames(decoder, &frame_count);
    if (result != MA_SUCCESS || frame_count == 0 || frame_count > (ma_uint64)INT_MAX)
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: decoded file is empty or too large: %s", label);
        return false;
    }

    pcm = (float *)ma_malloc((size_t)frame_count * (size_t)channels * sizeof(float), NULL);
    if (!pcm)
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: out of memory decoding: %s", label);
        return false;
    }

    result = ma_decoder_read_pcm_frames(decoder, pcm, frame_count, &frames_read);
    if (result != MA_SUCCESS || frames_read == 0)
    {
        ma_free(pcm, NULL);
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: failed to read decoded PCM from '%s': %s",
                     label, ma_result_description(result));
        return false;
    }

    if (frames_read > (ma_uint64)INT_MAX)
    {
        ma_free(pcm, NULL);
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: decoded file is empty or too large: %s", label);
        return false;
    }

    out->sample_rate = (int)sample_rate;
    out->channels = (int)channels;
    out->frame_count = (int)frames_read;
    out->samples = pcm;

    return true;
}

bool audio_decode_file(const char *path, AudioDecodedData *out, char *error, int error_size)
{
    if (!out)
        return false;

    memset(out, 0, sizeof(*out));

    if (!path || path[0] == '\0')
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: empty path");
        return false;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(path, &config, &decoder);
    if (result != MA_SUCCESS)
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: failed to decode '%s': %s",
                     path, ma_result_description(result));
        return false;
    }

    bool ok = audio_decode_from_decoder(&decoder, path, out, error, error_size);
    ma_decoder_uninit(&decoder);
    return ok;
}

bool audio_decode_memory(const uint8_t *data, size_t size, AudioDecodedData *out, char *error, int error_size)
{
    if (!out)
        return false;

    memset(out, 0, sizeof(*out));

    if (!data || size == 0)
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: empty buffer");
        return false;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_decoder decoder;
    ma_result result = ma_decoder_init_memory(data, size, &config, &decoder);
    if (result != MA_SUCCESS)
    {
        if (error && error_size > 0)
            snprintf(error, (size_t)error_size, "audio: failed to decode buffer: %s",
                     ma_result_description(result));
        return false;
    }

    bool ok = audio_decode_from_decoder(&decoder, "buffer", out, error, error_size);
    ma_decoder_uninit(&decoder);
    return ok;
}

void audio_decoded_data_free(AudioDecodedData *data)
{
    if (!data)
        return;
    if (data->samples)
        ma_free(data->samples, NULL);
    memset(data, 0, sizeof(*data));
}