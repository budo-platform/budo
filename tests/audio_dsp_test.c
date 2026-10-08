#include "audio/audio_dsp.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double cents(double got, double expected)
{
    return 1200.0 * log2(got / expected);
}

static void test_fft(void)
{
    enum { N = 64 };
    float re[N] = {0}, im[N] = {0};
    
    re[0] = 1.0f;
    assert(audio_dsp_fft(re, im, N, false));
    for (int k = 0; k < N; k++)
        assert(fabsf(re[k] - 1.0f) < 1e-6f && fabsf(im[k]) < 1e-6f);
    
    for (int i = 0; i < N; i++)
    {
        re[i] = (float)cos(2 * M_PI * 5 * i / N);
        im[i] = 0.0f;
    }
    assert(audio_dsp_fft(re, im, N, false));
    for (int k = 0; k < N; k++)
    {
        float expected = (k == 5 || k == N - 5) ? N / 2.0f : 0.0f;
        assert(fabsf(re[k] - expected) < 1e-4f && fabsf(im[k]) < 1e-4f);
    }
    
    float x[N], y[N];
    for (int i = 0; i < N; i++)
    {
        re[i] = x[i] = (float)sin(i * 0.37) + 0.2f * (float)(i % 7);
        im[i] = y[i] = (float)cos(i * 0.11);
    }
    assert(audio_dsp_fft(re, im, N, false));
    assert(audio_dsp_fft(re, im, N, true));
    for (int i = 0; i < N; i++)
        assert(fabsf(re[i] - x[i]) < 1e-5f && fabsf(im[i] - y[i]) < 1e-5f);
    
    assert(!audio_dsp_fft(re, im, 48, false));
    assert(!audio_dsp_fft(re, im, 1, false));
    assert(!audio_dsp_fft(NULL, im, N, false));
}

static void test_spectrum(void)
{
    enum { N = 2048 };
    const double rate = 48000;
    float samples[N], db[N / 2];
    
    double frequency = 100 * rate / N;
    for (int i = 0; i < N; i++)
        samples[i] = (float)sin(2 * M_PI * frequency * i / rate);
    assert(audio_dsp_spectrum_db(samples, N, db));
    int loudest = 0;
    for (int k = 1; k < N / 2; k++)
        if (db[k] > db[loudest])
            loudest = k;
    assert(loudest == 100);
    assert(fabsf(db[100]) < 0.1f);
    assert(db[300] < -60.0f);
    
    for (int i = 0; i < N; i++)
        samples[i] *= 0.5f;
    assert(audio_dsp_spectrum_db(samples, N, db));
    assert(fabsf(db[100] + 6.02f) < 0.1f);
    
    for (int i = 0; i < N; i++)
        samples[i] = 0.0f;
    assert(audio_dsp_spectrum_db(samples, N, db));
    assert(db[10] == AUDIO_DSP_MIN_DB);
    assert(!audio_dsp_spectrum_db(samples, 1000, db));
}

typedef double (*Signal)(double t);
static double s_frequency;
static double pure(double t) { return 0.3 * sin(2 * M_PI * s_frequency * t); }
static double weak_fundamental(double t)
{
    double f = s_frequency;
    return 0.05 * sin(2 * M_PI * f * t) + 0.3 * sin(2 * M_PI * 2 * f * t) + 0.2 * sin(2 * M_PI * 3 * f * t) +
           0.1 * sin(2 * M_PI * 4 * f * t);
}
static double missing_fundamental(double t)
{
    double v = 0;
    for (int h = 2; h <= 5; h++)
        v += 0.15 * sin(2 * M_PI * h * s_frequency * t + h);
    return v;
}
static double sawtooth(double t)
{
    double v = 0;
    for (int h = 1; h < 20; h++)
        v += sin(2 * M_PI * h * s_frequency * t) / h;
    return 0.2 * v;
}

static AudioPitch pitch_of(Signal signal, double frequency, size_t n, double rate)
{
    s_frequency = frequency;
    float *samples = (float *)malloc(n * sizeof(float));
    for (size_t i = 0; i < n; i++)
        samples[i] = (float)signal((double)i / rate);
    AudioPitch pitch;
    assert(audio_dsp_detect_pitch(samples, n, rate, NULL, &pitch));
    free(samples);
    return pitch;
}

static void test_pitch(void)
{
    const double rates[] = {44100, 48000};
    for (int r = 0; r < 2; r++)
    {
        double rate = rates[r];
        struct { Signal signal; double frequency; size_t n; } cases[] = {
            {pure, 440, 2048},           {pure, 82.41, 2048},         {pure, 2093, 1024},
            {pure, 3500, 512},           {weak_fundamental, 110, 2048}, {missing_fundamental, 196, 2048},
            {sawtooth, 261.63, 2048},    {sawtooth, 261.63, 1024},    {weak_fundamental, 220, 1024},
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            AudioPitch pitch = pitch_of(cases[i].signal, cases[i].frequency, cases[i].n, rate);
            if (!(pitch.frequency > 0 && fabs(cents(pitch.frequency, cases[i].frequency)) < 3.0))
            {
                fprintf(stderr, "case %zu at %.0f Hz: expected %.2f Hz, got %.2f (clarity %.2f)\n", i, rate,
                        cases[i].frequency, pitch.frequency, pitch.clarity);
                assert(0);
            }
            assert(pitch.clarity > 0.9);
        }
    }
    
    enum { N = 2048 };
    float samples[N];
    srand(7);
    for (int i = 0; i < N; i++)
        samples[i] = 0.3f * (2.0f * (float)rand() / (float)RAND_MAX - 1.0f);
    AudioPitch pitch;
    assert(audio_dsp_detect_pitch(samples, N, 48000, NULL, &pitch));
    assert(pitch.frequency == 0 && pitch.clarity < 0.7);
    for (int i = 0; i < N; i++)
        samples[i] = 0.0001f * (float)sin(2 * M_PI * 440 * i / 48000.0);
    assert(audio_dsp_detect_pitch(samples, N, 48000, NULL, &pitch));
    assert(pitch.frequency == 0 && pitch.level_db < -60);
    
    AudioPitchOptions options;
    audio_dsp_pitch_defaults(&options);
    options.min_frequency = 300;
    for (int i = 0; i < N; i++)
        samples[i] = 0.3f * (float)sin(2 * M_PI * 440 * i / 48000.0);
    assert(audio_dsp_detect_pitch(samples, N, 48000, &options, &pitch));
    assert(fabs(cents(pitch.frequency, 440)) < 3.0);
    options.min_frequency = 50;
    options.max_frequency = 300;
    assert(audio_dsp_detect_pitch(samples, N, 48000, &options, &pitch));
    assert(pitch.frequency == 0); 
    assert(!audio_dsp_detect_pitch(samples, 8, 48000, NULL, &pitch));
}

int main(void)
{
    test_fft();
    test_spectrum();
    test_pitch();
    puts("audio_dsp_test: ok");
    return 0;
}