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

    typedef struct AudioStreamDecoder AudioStreamDecoder;

    typedef struct AudioStreamDecoderInfo
    {
        int sample_rate; 
        int channels;
        int source_sample_rate; 
        int source_channels;
        int64_t length_frames;   
        int64_t position_frames; 
        bool ready;              
        bool ended;              
        bool needs_data;         
        bool seekable;
    } AudioStreamDecoderInfo;

    struct FileNativeReference;
    
    AudioStreamDecoder *audio_stream_decoder_open_file(void *file_native_reference, int sample_rate,
                                                       int channels, char *error, int error_size);
    AudioStreamDecoder *audio_stream_decoder_open_memory(const uint8_t *data, size_t size, int sample_rate,
                                                         int channels, char *error, int error_size);
    AudioStreamDecoder *audio_stream_decoder_create_push(int sample_rate, int channels);
    
    bool audio_stream_decoder_feed(AudioStreamDecoder *decoder, const uint8_t *data, size_t size, bool end,
                                   char *error, int error_size);

    int audio_stream_decoder_read(AudioStreamDecoder *decoder, float *out, int frames,
                                  char *error, int error_size);
    bool audio_stream_decoder_seek(AudioStreamDecoder *decoder, int64_t frame, char *error, int error_size);
    void audio_stream_decoder_info(AudioStreamDecoder *decoder, AudioStreamDecoderInfo *out);
    void audio_stream_decoder_close(AudioStreamDecoder *decoder);

#ifdef __cplusplus
}
#endif

#endif