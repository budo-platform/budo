#include "audio_decoder.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MA_NO_DEVICE_IO
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH

#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
#include "extras/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

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

#include "file/file_wrapper.h"

#define PUSH_READY_BYTES (64 * 1024)

#define PUSH_READ_FRAMES 2048

typedef enum
{
    DECODER_FILE,
    DECODER_MEMORY,
    DECODER_PUSH
} DecoderSource;

struct AudioStreamDecoder
{
    DecoderSource source;
    ma_decoder decoder;
    bool initialized;
    int sample_rate;
    int channels;
    int64_t length_frames; 
    bool ended;
    
    FileNativeReference file;
    uint64_t file_cursor;
    
    uint8_t *memory;
    
    uint8_t *bytes;
    size_t length;
    size_t capacity;
    uint64_t base;
    uint64_t cursor;
    bool input_ended;
};

static void decoder_error(char *error, int error_size, const char *message, ma_result result)
{
    if (error && error_size > 0)
        snprintf(error, (size_t)error_size, "audio: %s%s%s", message,
                 result != MA_SUCCESS ? ": " : "", result != MA_SUCCESS ? ma_result_description(result) : "");
}

static ma_result file_on_read(ma_decoder *decoder, void *out, size_t size, size_t *read)
{
    AudioStreamDecoder *stream = (AudioStreamDecoder *)decoder->pUserData;
    int64_t got = file_native_read(&stream->file, stream->file_cursor, out, size);
    if (got < 0)
    {
        *read = 0;
        return MA_IO_ERROR;
    }
    stream->file_cursor += (uint64_t)got;
    *read = (size_t)got;
    return got == 0 && size > 0 ? MA_AT_END : MA_SUCCESS;
}

static ma_result file_on_seek(ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin)
{
    AudioStreamDecoder *stream = (AudioStreamDecoder *)decoder->pUserData;
    int64_t base = origin == ma_seek_origin_start ? 0
                   : origin == ma_seek_origin_current ? (int64_t)stream->file_cursor
                                                      : (int64_t)stream->file.size;
    int64_t target = base + offset;
    if (target < 0 || (uint64_t)target > stream->file.size)
        return MA_INVALID_ARGS;
    stream->file_cursor = (uint64_t)target;
    return MA_SUCCESS;
}

static ma_result push_on_read(ma_decoder *decoder, void *out, size_t size, size_t *read)
{
    AudioStreamDecoder *stream = (AudioStreamDecoder *)decoder->pUserData;
    size_t at = (size_t)(stream->cursor - stream->base);
    size_t available = stream->length > at ? stream->length - at : 0;
    size_t count = size < available ? size : available;
    memcpy(out, stream->bytes + at, count);
    stream->cursor += count;
    *read = count;
    return count == 0 && size > 0 ? MA_AT_END : MA_SUCCESS;
}

static ma_result push_on_seek(ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin)
{
    AudioStreamDecoder *stream = (AudioStreamDecoder *)decoder->pUserData;
    if (origin == ma_seek_origin_end)
        return MA_NOT_IMPLEMENTED;
    int64_t target = (origin == ma_seek_origin_start ? 0 : (int64_t)stream->cursor) + offset;
    if (target < (int64_t)stream->base || target > (int64_t)(stream->base + stream->length))
        return MA_INVALID_OPERATION;
    stream->cursor = (uint64_t)target;
    return MA_SUCCESS;
}

static AudioStreamDecoder *decoder_new(DecoderSource source, int sample_rate, int channels)
{
    AudioStreamDecoder *stream = (AudioStreamDecoder *)calloc(1, sizeof(*stream));
    if (!stream)
        return NULL;
    stream->source = source;
    stream->sample_rate = sample_rate > 0 ? sample_rate : 0; 
    stream->channels = channels == 1 || channels == 2 ? channels : 2;
    stream->length_frames = -2;
#ifndef _WIN32
    stream->file.fd = -1;
#endif
    return stream;
}

static ma_decoder_config decoder_config(const AudioStreamDecoder *stream)
{
    return ma_decoder_config_init(ma_format_f32, (ma_uint32)stream->channels, (ma_uint32)stream->sample_rate);
}

static void decoder_adopt_format(AudioStreamDecoder *stream)
{
    ma_format format;
    ma_uint32 channels = 0, rate = 0;
    if (ma_decoder_get_data_format(&stream->decoder, &format, &channels, &rate, NULL, 0) == MA_SUCCESS)
    {
        stream->sample_rate = (int)rate;
        stream->channels = (int)channels;
    }
}

AudioStreamDecoder *audio_stream_decoder_open_file(void *file_native_reference, int sample_rate, int channels,
                                                   char *error, int error_size)
{
    FileNativeReference *file = (FileNativeReference *)file_native_reference;
    AudioStreamDecoder *stream = file ? decoder_new(DECODER_FILE, sample_rate, channels) : NULL;
    if (!stream)
    {
        if (file)
            file_native_close(file);
        decoder_error(error, error_size, "out of memory opening a decoder", MA_SUCCESS);
        return NULL;
    }
    stream->file = *file;
    ma_decoder_config config = decoder_config(stream);
    ma_result result = ma_decoder_init(file_on_read, file_on_seek, stream, &config, &stream->decoder);
    if (result != MA_SUCCESS)
    {
        decoder_error(error, error_size, "not a decodable audio file (WAV, MP3, Ogg Vorbis, FLAC)", result);
        file_native_close(&stream->file);
        free(stream);
        return NULL;
    }
    stream->initialized = true;
    decoder_adopt_format(stream);
    return stream;
}

AudioStreamDecoder *audio_stream_decoder_open_memory(const uint8_t *data, size_t size, int sample_rate,
                                                     int channels, char *error, int error_size)
{
    if (!data || size == 0)
    {
        decoder_error(error, error_size, "empty audio data", MA_SUCCESS);
        return NULL;
    }
    AudioStreamDecoder *stream = decoder_new(DECODER_MEMORY, sample_rate, channels);
    if (stream)
        stream->memory = (uint8_t *)malloc(size);
    if (!stream || !stream->memory)
    {
        free(stream);
        decoder_error(error, error_size, "out of memory opening a decoder", MA_SUCCESS);
        return NULL;
    }
    memcpy(stream->memory, data, size);
    ma_decoder_config config = decoder_config(stream);
    ma_result result = ma_decoder_init_memory(stream->memory, size, &config, &stream->decoder);
    if (result != MA_SUCCESS)
    {
        decoder_error(error, error_size, "not decodable audio data (WAV, MP3, Ogg Vorbis, FLAC)", result);
        free(stream->memory);
        free(stream);
        return NULL;
    }
    stream->initialized = true;
    decoder_adopt_format(stream);
    return stream;
}

AudioStreamDecoder *audio_stream_decoder_create_push(int sample_rate, int channels)
{
    return decoder_new(DECODER_PUSH, sample_rate, channels);
}

static size_t push_ahead(const AudioStreamDecoder *stream)
{
    size_t at = (size_t)(stream->cursor - stream->base);
    return stream->length > at ? stream->length - at : 0;
}

bool audio_stream_decoder_feed(AudioStreamDecoder *stream, const uint8_t *data, size_t size, bool end,
                               char *error, int error_size)
{
    if (!stream || stream->source != DECODER_PUSH)
    {
        decoder_error(error, error_size, "only a push decoder takes bytes", MA_SUCCESS);
        return false;
    }
    if (stream->input_ended && size > 0)
    {
        decoder_error(error, error_size, "bytes fed after the end of the input", MA_SUCCESS);
        return false;
    }
    
    size_t consumed = (size_t)(stream->cursor - stream->base);
    if (stream->initialized && consumed > PUSH_READY_BYTES)
    {
        size_t drop = consumed - PUSH_READY_BYTES / 4;
        memmove(stream->bytes, stream->bytes + drop, stream->length - drop);
        stream->length -= drop;
        stream->base += drop;
    }
    if (size > 0)
    {
        if (stream->length + size > stream->capacity)
        {
            size_t capacity = stream->capacity ? stream->capacity : 64 * 1024;
            while (capacity < stream->length + size)
                capacity *= 2;
            uint8_t *grown = (uint8_t *)realloc(stream->bytes, capacity);
            if (!grown)
            {
                decoder_error(error, error_size, "out of memory buffering audio bytes", MA_SUCCESS);
                return false;
            }
            stream->bytes = grown;
            stream->capacity = capacity;
        }
        memcpy(stream->bytes + stream->length, data, size);
        stream->length += size;
    }
    if (end)
        stream->input_ended = true;
    return true;
}

static bool push_can_decode(const AudioStreamDecoder *stream)
{
    return stream->input_ended || push_ahead(stream) >= PUSH_READY_BYTES;
}

static bool push_try_init(AudioStreamDecoder *stream, char *error, int error_size)
{
    if (stream->initialized)
        return true;
    if (!push_can_decode(stream))
        return false;
    ma_decoder_config config = decoder_config(stream);
    stream->cursor = stream->base;
    ma_result result = ma_decoder_init(push_on_read, push_on_seek, stream, &config, &stream->decoder);
    if (result != MA_SUCCESS)
    {
        decoder_error(error, error_size, "not decodable audio data (WAV, MP3, Ogg Vorbis, FLAC)", result);
        return false;
    }
    stream->initialized = true;
    decoder_adopt_format(stream);
    return true;
}

int audio_stream_decoder_read(AudioStreamDecoder *stream, float *out, int frames, char *error, int error_size)
{
    if (!stream || !out || frames < 0)
        return -1;
    if (frames == 0 || stream->ended)
        return 0;
    if (stream->source == DECODER_PUSH && !stream->initialized)
    {
        if (!push_try_init(stream, error, error_size))
            return push_can_decode(stream) ? -1 : 0;
    }
    int done = 0;
    while (done < frames)
    {
        int want = frames - done;
        if (stream->source == DECODER_PUSH)
        {
            if (!push_can_decode(stream))
                break;
            if (want > PUSH_READ_FRAMES)
                want = PUSH_READ_FRAMES;
        }
        ma_uint64 got = 0;
        ma_result result = ma_decoder_read_pcm_frames(&stream->decoder, out + (size_t)done * (size_t)stream->channels,
                                                      (ma_uint64)want, &got);
        done += (int)got;
        if (result == MA_AT_END || (got == 0 && result == MA_SUCCESS))
        {

            if (stream->source != DECODER_PUSH || stream->input_ended)
                stream->ended = true;
            break;
        }
        if (result != MA_SUCCESS)
        {
            decoder_error(error, error_size, "decoding failed", result);
            return done > 0 ? done : -1;
        }
    }
    return done;
}

bool audio_stream_decoder_seek(AudioStreamDecoder *stream, int64_t frame, char *error, int error_size)
{
    if (!stream || !stream->initialized || stream->source == DECODER_PUSH)
    {
        decoder_error(error, error_size, "this decoder cannot seek (push decoders read straight through)",
                      MA_SUCCESS);
        return false;
    }
    ma_result result = ma_decoder_seek_to_pcm_frame(&stream->decoder, (ma_uint64)(frame > 0 ? frame : 0));
    if (result != MA_SUCCESS)
    {
        decoder_error(error, error_size, "seek failed", result);
        return false;
    }
    stream->ended = false;
    return true;
}

void audio_stream_decoder_info(AudioStreamDecoder *stream, AudioStreamDecoderInfo *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->length_frames = -1;
    if (!stream)
        return;
    out->sample_rate = stream->sample_rate;
    out->channels = stream->channels;
    out->ready = stream->initialized;
    out->ended = stream->ended;
    out->seekable = stream->initialized && stream->source != DECODER_PUSH;
    out->needs_data = stream->source == DECODER_PUSH && !stream->ended && !push_can_decode(stream);
    if (!stream->initialized)
        return;
    
    ma_format format;
    ma_uint32 channels = 0, rate = 0;
    if (stream->decoder.pBackend &&
        ma_data_source_get_data_format(stream->decoder.pBackend, &format, &channels, &rate, NULL, 0) == MA_SUCCESS)
    {
        out->source_channels = (int)channels;
        out->source_sample_rate = (int)rate;
    }
    ma_uint64 cursor = 0;
    if (ma_decoder_get_cursor_in_pcm_frames(&stream->decoder, &cursor) == MA_SUCCESS)
        out->position_frames = (int64_t)cursor;

    if (stream->source != DECODER_PUSH && stream->length_frames == -2)
    {
        ma_uint64 length = 0;
        stream->length_frames = ma_decoder_get_length_in_pcm_frames(&stream->decoder, &length) == MA_SUCCESS &&
                                        length > 0
                                    ? (int64_t)length
                                    : -1;
    }
    out->length_frames = stream->source == DECODER_PUSH ? -1 : stream->length_frames;
}

void audio_stream_decoder_close(AudioStreamDecoder *stream)
{
    if (!stream)
        return;
    if (stream->initialized)
        ma_decoder_uninit(&stream->decoder);
    if (stream->source == DECODER_FILE)
        file_native_close(&stream->file);
    free(stream->memory);
    free(stream->bytes);
    free(stream);
}