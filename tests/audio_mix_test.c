#include "audio/audio_mix.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void test_step_keeps_precision_late_in_long_files(void)
{
    
    const int frames = 44100 * 600;
    float *data = (float *)malloc(sizeof(float) * (size_t)frames);
    assert(data);
    for (int i = 0; i < frames; i++)
        data[i] = (float)i / (float)frames;

    const double step = 44100.0 / 48000.0;
    
    double position = 44100.0 * 480.0;
    const double start = position;
    float left = 0, right = 0;
    for (int s = 0; s < 48000; s++)
        assert(audio_mix_buffer_frame(data, frames, 1, false, 1.0f, &position, step, &left, &right));
    
    assert(fabs((position - start) - 44100.0) < 1e-3);

    float old = (float)start;
    for (int s = 0; s < 48000; s++)
        old += (float)step;
    assert(fabs(((double)old - start) - 44100.0) > 1000.0);
    free(data);
}

static void test_interpolates_between_frames(void)
{
    const float stereo[] = {0.0f, 1.0f, 1.0f, 0.0f};
    double position = 0.5;
    float left = 0, right = 0;
    assert(audio_mix_buffer_frame(stereo, 2, 2, false, 1.0f, &position, 1.0, &left, &right));
    assert(fabsf(left - 0.5f) < 1e-6f && fabsf(right - 0.5f) < 1e-6f);
    assert(fabs(position - 1.5) < 1e-12);
}

static void test_end_and_loop(void)
{
    const float mono[] = {0.25f, 0.75f};
    double position = 2.0;
    float left = 0, right = 0;
    
    assert(!audio_mix_buffer_frame(mono, 2, 1, false, 1.0f, &position, 1.0, &left, &right));
    
    position = 2.5;
    assert(audio_mix_buffer_frame(mono, 2, 1, true, 2.0f, &position, 1.0, &left, &right));
    assert(fabs(position - 1.5) < 1e-12);
    assert(fabsf(left - 1.0f) < 1e-6f); 
    position = 1.5;
    left = right = 0;
    assert(audio_mix_buffer_frame(mono, 2, 1, true, 1.0f, &position, 1.0, &left, &right));
    assert(fabsf(left - 0.5f) < 1e-6f); 
}

int main(void)
{
    test_step_keeps_precision_late_in_long_files();
    test_interpolates_between_frames();
    test_end_and_loop();
    puts("audio_mix_test: ok");
    return 0;
}