#include "audio_dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool audio_dsp_is_power_of_two(size_t n)
{
    return n >= 2 && (n & (n - 1)) == 0;
}

static void fft_double(double *re, double *im, size_t n, bool inverse)
{
    for (size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j |= bit;
        if (i < j)
        {
            double t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (size_t half = 1; half < n; half <<= 1)
    {
        double angle = (inverse ? M_PI : -M_PI) / (double)half;
        double step_r = cos(angle), step_i = sin(angle);
        double wr = 1.0, wi = 0.0;
        for (size_t k = 0; k < half; k++)
        {
            for (size_t a = k; a < n; a += half * 2)
            {
                size_t b = a + half;
                double tr = re[b] * wr - im[b] * wi;
                double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
            double next = wr * step_r - wi * step_i;
            wi = wr * step_i + wi * step_r;
            wr = next;
        }
    }
}

bool audio_dsp_fft(float *re, float *im, size_t n, bool inverse)
{
    if (!re || !im || !audio_dsp_is_power_of_two(n) || n > AUDIO_DSP_MAX_SIZE)
        return false;
    double *work = (double *)malloc(n * 2 * sizeof(double));
    if (!work)
        return false;
    double *wre = work, *wim = work + n;
    for (size_t i = 0; i < n; i++)
    {
        wre[i] = re[i];
        wim[i] = im[i];
    }
    fft_double(wre, wim, n, inverse);
    double scale = inverse ? 1.0 / (double)n : 1.0;
    for (size_t i = 0; i < n; i++)
    {
        re[i] = (float)(wre[i] * scale);
        im[i] = (float)(wim[i] * scale);
    }
    free(work);
    return true;
}

bool audio_dsp_spectrum_db(const float *samples, size_t n, float *out_db)
{
    if (!samples || !out_db || n < 4 || !audio_dsp_is_power_of_two(n) || n > AUDIO_DSP_MAX_SIZE)
        return false;
    double *work = (double *)malloc(n * 2 * sizeof(double));
    if (!work)
        return false;
    double *re = work, *im = work + n;
    for (size_t i = 0; i < n; i++)
    {
        double window = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(n - 1));
        re[i] = samples[i] * window;
        im[i] = 0.0;
    }
    fft_double(re, im, n, false);
    
    double scale = 4.0 / (double)n;
    for (size_t k = 0; k < n / 2; k++)
    {
        double magnitude = sqrt(re[k] * re[k] + im[k] * im[k]) * scale;
        double db = magnitude > 1e-9 ? 20.0 * log10(magnitude) : AUDIO_DSP_MIN_DB;
        out_db[k] = (float)(db < AUDIO_DSP_MIN_DB ? AUDIO_DSP_MIN_DB : db);
    }
    free(work);
    return true;
}

void audio_dsp_pitch_defaults(AudioPitchOptions *options)
{
    options->min_frequency = 50.0;
    options->max_frequency = 4000.0;
    options->min_clarity = 0.7;
    options->min_level_db = -60.0;
}

#define PITCH_PEAK_RATIO 0.9

bool audio_dsp_detect_pitch(const float *samples, size_t n, double sample_rate,
                            const AudioPitchOptions *options, AudioPitch *out)
{
    if (!samples || !out || n < 16 || n > AUDIO_DSP_MAX_SIZE / 2 || !(sample_rate > 0))
        return false;
    AudioPitchOptions o;
    audio_dsp_pitch_defaults(&o);
    if (options)
        o = *options;
    memset(out, 0, sizeof(*out));

    double energy = 0.0;
    for (size_t i = 0; i < n; i++)
        energy += (double)samples[i] * samples[i];
    out->level_db = 10.0 * log10(energy / (double)n + 1e-20);
    if (out->level_db < o.min_level_db || energy <= 1e-12)
        return true;

    size_t max_tau = n / 2;
    if (o.min_frequency > 0 && sample_rate / o.min_frequency < (double)max_tau)
        max_tau = (size_t)ceil(sample_rate / o.min_frequency);
    size_t min_tau = o.max_frequency > 0 ? (size_t)floor(sample_rate / o.max_frequency) : 2;
    if (min_tau < 2)
        min_tau = 2;
    if (max_tau + 2 >= n || min_tau >= max_tau)
        return true;

    size_t m = 2;
    while (m < 2 * n)
        m <<= 1;
    double *work = (double *)calloc(m * 2 + max_tau + 2, sizeof(double));
    if (!work)
        return false;
    double *re = work, *im = work + m, *nsdf = work + 2 * m;
    for (size_t i = 0; i < n; i++)
        re[i] = samples[i];
    fft_double(re, im, m, false);
    for (size_t k = 0; k < m; k++)
    {
        re[k] = re[k] * re[k] + im[k] * im[k];
        im[k] = 0.0;
    }
    fft_double(re, im, m, true);

    double overlap = 2.0 * energy;
    for (size_t tau = 0; tau <= max_tau + 1; tau++)
    {
        if (tau > 0)
            overlap -= (double)samples[tau - 1] * samples[tau - 1] + (double)samples[n - tau] * samples[n - tau];
        nsdf[tau] = overlap > 1e-12 ? 2.0 * (re[tau] / (double)m) / overlap : 0.0;
    }

    size_t tau = 1;
    while (tau < max_tau && nsdf[tau] > 0)
        tau++;
    size_t peaks[512];
    size_t peak_count = 0;
    size_t best = 0;
    bool in_lobe = false;
    double highest = 0.0;
    for (; tau <= max_tau; tau++)
    {
        if (nsdf[tau] > 0 && nsdf[tau - 1] <= 0)
        {
            in_lobe = true;
            best = tau;
        }
        if (!in_lobe)
            continue;
        if (nsdf[tau] > nsdf[best])
            best = tau;
        if (nsdf[tau] <= 0 || tau == max_tau)
        {
            if (peak_count < sizeof(peaks) / sizeof(peaks[0]))
            {
                peaks[peak_count++] = best;
                if (nsdf[best] > highest)
                    highest = nsdf[best];
            }
            in_lobe = false;
        }
    }
    size_t chosen = 0;
    for (size_t i = 0; i < peak_count; i++)
        if (nsdf[peaks[i]] >= PITCH_PEAK_RATIO * highest)
        {
            chosen = peaks[i];
            break;
        }

    if (chosen == 0 || chosen < min_tau)
    {
        free(work);
        return true;
    }

    double a = nsdf[chosen - 1], b = nsdf[chosen], c = nsdf[chosen + 1];
    double denominator = a - 2.0 * b + c;
    double delta = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
    if (delta < -0.5)
        delta = -0.5;
    if (delta > 0.5)
        delta = 0.5;
    out->clarity = b - 0.25 * (a - c) * delta;
    if (out->clarity > 1.0)
        out->clarity = 1.0;
    if (out->clarity >= o.min_clarity)
        out->frequency = sample_rate / ((double)chosen + delta);
    free(work);
    return true;
}