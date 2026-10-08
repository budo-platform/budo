#ifndef BUDO_AUDIO_MIX_H
#define BUDO_AUDIO_MIX_H

#include <math.h>
#include <stdbool.h>

static inline bool audio_mix_buffer_frame(const float *data, int num_frames, int channels,
                                          bool loop, float gain, double *position,
                                          double step, float *left, float *right)
{
    if (num_frames <= 0)
        return false;
    if (*position >= (double)num_frames)
    {
        if (!loop)
            return false;
        *position = fmod(*position, (double)num_frames);
    }
    int frame = (int)*position;
    float frac = (float)(*position - (double)frame);
    int next = frame + 1;
    if (next >= num_frames)
        next = loop ? 0 : frame;

    if (channels == 1)
    {
        float a = data[frame], b = data[next];
        float sample = (a + (b - a) * frac) * gain;
        *left += sample;
        *right += sample;
    }
    else
    {
        float al = data[frame * 2], bl = data[next * 2];
        float ar = data[frame * 2 + 1], br = data[next * 2 + 1];
        *left += (al + (bl - al) * frac) * gain;
        *right += (ar + (br - ar) * frac) * gain;
    }

    *position += step;
    return true;
}

#endif