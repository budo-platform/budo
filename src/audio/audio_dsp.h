#ifndef BUDO_AUDIO_DSP_H
#define BUDO_AUDIO_DSP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define AUDIO_DSP_MAX_SIZE ((size_t)1 << 20)

#define AUDIO_DSP_MIN_DB (-180.0f)

    bool audio_dsp_is_power_of_two(size_t n);

    bool audio_dsp_fft(float *re, float *im, size_t n, bool inverse);

    bool audio_dsp_spectrum_db(const float *samples, size_t n, float *out_db);

    typedef struct AudioPitchOptions
    {
        double min_frequency; 
        double max_frequency; 
        double min_clarity;   
        double min_level_db;  
    } AudioPitchOptions;

    typedef struct AudioPitch
    {
        double frequency; 
        double clarity;   
        double level_db;  
    } AudioPitch;

    void audio_dsp_pitch_defaults(AudioPitchOptions *options);

    bool audio_dsp_detect_pitch(const float *samples, size_t n, double sample_rate,
                                const AudioPitchOptions *options, AudioPitch *out);

#ifdef __cplusplus
}
#endif

#endif