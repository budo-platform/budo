#include "audio/audio_wrapper.h"
#include "audio/audio_decoder.h"

#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_BUFFER_PLAYBACKS 32

struct AudioContext
{
    bool playing;
    float master_gain;
    char asset_root[1024];
    char error_msg[256];
};

static bool audio_path_is_absolute(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}

static bool audio_path_is_safe_relative(const char *path)
{
    if (!path || path[0] == '\0' || audio_path_is_absolute(path))
        return false;

    const char *segment = path;
    while (*segment)
    {
        const char *next = segment;
        while (*next && *next != '/' && *next != '\\')
            next++;

        size_t len = (size_t)(next - segment);
        if (len == 0 || (len == 1 && segment[0] == '.') ||
            (len == 2 && segment[0] == '.' && segment[1] == '.'))
            return false;

        segment = *next ? next + 1 : next;
    }

    return true;
}

static bool audio_resolve_asset_path(AudioContext *ctx, const char *path,
                                     char *out, size_t out_size)
{
    if (!ctx || !audio_path_is_safe_relative(path))
    {
        if (ctx)
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: unsafe asset path: %s", path ? path : "(null)");
        return false;
    }

    const char *root = ctx->asset_root[0] ? ctx->asset_root : "/";
    const char *sep = (strcmp(root, "/") == 0) ? "" : "/";
    int written = snprintf(out, out_size, "%s%s%s", root, sep, path);
    if (written < 0 || (size_t)written >= out_size)
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: asset path is too long");
        return false;
    }

    return true;
}

static uint8_t *audio_read_asset_file(AudioContext *ctx, const char *path, size_t *out_size)
{
    char resolved[1024];
    FILE *file;
    long size;
    uint8_t *data;

    if (out_size)
        *out_size = 0;

    if (!audio_resolve_asset_path(ctx, path, resolved, sizeof(resolved)))
        return NULL;

    file = fopen(resolved, "rb");
    if (!file)
    {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: failed to open '%s'", path ? path : "(null)");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: failed to read file size: %s", path ? path : "(null)");
        return NULL;
    }
    size = ftell(file);
    if (size <= 0)
    {
        fclose(file);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: decoded file is empty or too large: %s", path ? path : "(null)");
        return NULL;
    }
    rewind(file);

    data = (uint8_t *)malloc((size_t)size);
    if (!data)
    {
        fclose(file);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: out of memory reading: %s", path ? path : "(null)");
        return NULL;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size)
    {
        free(data);
        fclose(file);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: failed to read: %s", path ? path : "(null)");
        return NULL;
    }

    fclose(file);
    if (out_size)
        *out_size = (size_t)size;
    return data;
}

EM_JS(int, js_hw_audio_init, (void), {
    if (Module._hw_audio)
        return 1;

    var AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx)
        return 0;

    var ctx = new AudioCtx();
    var master = ctx.createGain();
    master.gain.value = 0.5;
    master.connect(ctx.destination);

    var noiseBuf = ctx.createBuffer(1, 88200, 44100);
    var noiseData = noiseBuf.getChannelData(0);
    for (var i = 0; i < 88200; i++)
    {
        noiseData[i] = Math.random() * 2 - 1;
    }

    Module._hw_audio = {
        ctx : ctx,
        masterGain : master,
        oscs : [],      
        bufs : [],      
        playbacks : [], 
        noiseBuffer : noiseBuf
    };

    Module._hw_audio_make_source = function(slot)
    {
        var ac = Module._hw_audio.ctx;
        var now = ac.currentTime;

        if (slot.oscNode)
        {
            try { slot.oscNode.stop(); }
            catch(e) {}
            slot.oscNode = null;
        }
        if (slot.noiseNode)
        {
            try { slot.noiseNode.stop(); }
            catch(e) {}
            slot.noiseNode = null;
        }

        if (slot.type == 4)
        {
            
            var src = ac.createBufferSource();
            src.buffer = Module._hw_audio.noiseBuffer;
            src.loop = true;
            src.connect(slot.envGain);
            src.start();
            slot.noiseNode = src;
        }
        else
        {
            var types = [ 'sine', 'square', 'sawtooth', 'triangle' ];
            var osc = ac.createOscillator();
            osc.type = types[slot.type] || 'sine';
            osc.frequency.setValueAtTime(slot.frequency, now);
            osc.detune.setValueAtTime(slot.detune, now);
            osc.connect(slot.envGain);
            osc.start();
            slot.oscNode = osc;
        }
    };

    var resumeFn = function()
    {
        if (Module._hw_audio && Module._hw_audio.ctx.state == 'suspended')
        {
            Module._hw_audio.ctx.resume();
        }
        document.removeEventListener('click', resumeFn);
        document.removeEventListener('touchstart', resumeFn);
        document.removeEventListener('keydown', resumeFn);
    };
    document.addEventListener('click', resumeFn);
    document.addEventListener('touchstart', resumeFn);
    document.addEventListener('keydown', resumeFn);

    return 1;
});

EM_JS(void, js_hw_audio_destroy, (void), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;

    for (var i = 0; i < a.oscs.length; i++)
    {
        var s = a.oscs[i];
        if (!s)
            continue;
        if (s.oscNode)
        {
            try { s.oscNode.stop(); }
            catch(e) {}
        }
        if (s.noiseNode)
        {
            try { s.noiseNode.stop(); }
            catch(e) {}
        }
    }

    for (var i = 0; i < a.playbacks.length; i++)
    {
        var p = a.playbacks[i];
        if (p && p.active)
        {
            try { p.source.stop(); }
            catch(e) {}
        }
    }

    a.ctx.close();
    Module._hw_audio = null;
});

EM_JS(void, js_hw_audio_resume, (void), {
    if (!Module._hw_audio)
        return;
    Module._hw_audio.ctx.resume();
});

EM_JS(void, js_hw_audio_suspend, (void), {
    if (!Module._hw_audio)
        return;
    Module._hw_audio.ctx.suspend();
});

EM_JS(int, js_hw_audio_is_running, (void), {
    if (!Module._hw_audio)
        return 0;
    return Module._hw_audio.ctx.state == 'running' ? 1 : 0;
});

EM_JS(void, js_hw_audio_set_master_gain, (float gain), {
    if (!Module._hw_audio)
        return;
    Module._hw_audio.masterGain.gain.setValueAtTime(gain,
                                                    Module._hw_audio.ctx.currentTime);
});

EM_JS(float, js_hw_audio_get_master_gain, (void), {
    if (!Module._hw_audio)
        return 0.0;
    return Module._hw_audio.masterGain.gain.value;
});

EM_JS(int, js_hw_audio_create_osc, (void), {
    if (!Module._hw_audio)
        return -1;
    var a = Module._hw_audio;
    var ctx = a.ctx;

    var id = -1;
    for (var i = 0; i < a.oscs.length; i++)
    {
        if (!a.oscs[i])
        {
            id = i;
            break;
        }
    }
    if (id < 0)
    {
        if (a.oscs.length >= 32)
            return -1; 
        id = a.oscs.length;
    }

    var envGain = ctx.createGain();
    var oscGain = ctx.createGain();
    envGain.gain.value = 0.0;
    oscGain.gain.value = 0.5;
    envGain.connect(oscGain);
    oscGain.connect(a.masterGain);

    a.oscs[id] = {
        envGain : envGain,
        oscGain : oscGain,
        oscNode : null,
        noiseNode : null,
        type : 0, 
        frequency : 440.0,
        detune : 0.0,
        attack : 0.0,
        decay : 0.0,
        sustain : 1.0,
        release_t : 0.0,
        playing : false
    };

    return id;
});

EM_JS(void, js_hw_audio_destroy_osc, (int id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];

    if (s.oscNode)
    {
        try { s.oscNode.stop(); }
        catch(e) {}
    }
    if (s.noiseNode)
    {
        try { s.noiseNode.stop(); }
        catch(e) {}
    }
    s.envGain.disconnect();
    s.oscGain.disconnect();
    a.oscs[id] = null;
});

EM_JS(void, js_hw_audio_osc_set_type, (int id, int type), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];

    var oldType = s.type;
    s.type = type;

    if (s.playing)
    {
        var wasNoise = (oldType == 4);
        var isNoise = (type == 4);
        if (wasNoise != isNoise)
        {
            
            var ac = a.ctx;
            var now = ac.currentTime;
            var env = s.envGain.gain.value;
            Module._hw_audio_make_source(s);
            
            s.envGain.gain.cancelScheduledValues(now);
            s.envGain.gain.setValueAtTime(env, now);
        }
        else if (!isNoise && s.oscNode)
        {
            
            var types = [ 'sine', 'square', 'sawtooth', 'triangle' ];
            s.oscNode.type = types[type] || 'sine';
        }
    }
});

EM_JS(void, js_hw_audio_osc_set_freq, (int id, float freq), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    s.frequency = freq;
    if (s.oscNode)
    {
        s.oscNode.frequency.setValueAtTime(freq, a.ctx.currentTime);
    }
});

EM_JS(void, js_hw_audio_osc_set_gain, (int id, float gain), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    a.oscs[id].oscGain.gain.setValueAtTime(gain, a.ctx.currentTime);
});

EM_JS(void, js_hw_audio_osc_set_detune, (int id, float cents), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    s.detune = cents;
    if (s.oscNode)
    {
        s.oscNode.detune.setValueAtTime(cents, a.ctx.currentTime);
    }
});

EM_JS(void, js_hw_audio_osc_start, (int id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    var now = a.ctx.currentTime;

    Module._hw_audio_make_source(s);
    s.envGain.gain.cancelScheduledValues(now);
    s.envGain.gain.setValueAtTime(1.0, now);
    s.playing = true;
});

EM_JS(void, js_hw_audio_osc_stop, (int id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    var now = a.ctx.currentTime;

    if (s.oscNode)
    {
        try { s.oscNode.stop(); }
        catch(e) {}
        s.oscNode = null;
    }
    if (s.noiseNode)
    {
        try { s.noiseNode.stop(); }
        catch(e) {}
        s.noiseNode = null;
    }
    s.envGain.gain.cancelScheduledValues(now);
    s.envGain.gain.setValueAtTime(0.0, now);
    s.playing = false;
});

EM_JS(void, js_hw_audio_osc_set_env, (int id, float attack, float decay, float sustain, float release_t), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    s.attack = attack;
    s.decay = decay;
    s.sustain = sustain;
    s.release_t = release_t;
});

EM_JS(void, js_hw_audio_osc_note_on, (int id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    var now = a.ctx.currentTime;

    Module._hw_audio_make_source(s);

    var g = s.envGain.gain;
    g.cancelScheduledValues(now);
    g.setValueAtTime(0.0, now);

    var atkEnd = now + Math.max(s.attack, 0.001);
    var decayEnd = atkEnd + Math.max(s.decay, 0.001);

    g.linearRampToValueAtTime(1.0, atkEnd);
    g.linearRampToValueAtTime(s.sustain, decayEnd);

    s.playing = true;
});

EM_JS(void, js_hw_audio_osc_note_off, (int id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.oscs.length || !a.oscs[id])
        return;
    var s = a.oscs[id];
    if (!s.playing)
        return;
    var now = a.ctx.currentTime;

    var g = s.envGain.gain;
    g.cancelScheduledValues(now);
    
    g.setValueAtTime(g.value, now);

    var relEnd = now + Math.max(s.release_t, 0.001);
    g.linearRampToValueAtTime(0.0, relEnd);

    var releaseMs = Math.max(s.release_t, 0.001) * 1000 + 50;
    var oscRef = s.oscNode;
    var noiseRef = s.noiseNode;
    setTimeout(function() {
        if (oscRef)   { try { oscRef.stop(); }   catch(e) {} }
        if (noiseRef) { try { noiseRef.stop(); } catch(e) {} } }, releaseMs);

    s.oscNode = null;
    s.noiseNode = null;
    s.playing = false;
});

EM_JS(int, js_hw_audio_create_buf, (int sample_rate, int channels, int num_samples), {
    if (!Module._hw_audio)
        return -1;
    var a = Module._hw_audio;

    var id = -1;
    for (var i = 0; i < a.bufs.length; i++)
    {
        if (!a.bufs[i])
        {
            id = i;
            break;
        }
    }
    if (id < 0)
    {
        if (a.bufs.length >= 64)
            return -1; 
        id = a.bufs.length;
    }

    a.bufs[id] = {
        sampleRate : sample_rate,
        channels : channels,
        numSamples : num_samples,
        data : new Float32Array(num_samples * channels)
    };
    return id;
});

EM_JS(void, js_hw_audio_destroy_buf, (int id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.bufs.length || !a.bufs[id])
        return;

    for (var i = 0; i < a.playbacks.length; i++)
    {
        var p = a.playbacks[i];
        if (p && p.active && p.bufferId == id)
        {
            try { p.source.stop(); }
            catch(e) {}
            p.active = false;
        }
    }

    a.bufs[id] = null;
});

EM_JS(void, js_hw_audio_buf_set_data, (int id, const float *samples_ptr, int offset, int count), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (id < 0 || id >= a.bufs.length || !a.bufs[id])
        return;
    var buf = a.bufs[id];
    var total = buf.numSamples * buf.channels;
    if (offset < 0 || offset >= total)
        return;
    if (offset + count > total)
        count = total - offset;

    var src = new Float32Array(HEAPF32.buffer, samples_ptr, count);
    buf.data.set(src, offset);
});

EM_JS(int, js_hw_audio_play_buf, (int buf_id, int loop, float gain), {
    if (!Module._hw_audio)
        return -1;
    var a = Module._hw_audio;
    if (buf_id < 0 || buf_id >= a.bufs.length || !a.bufs[buf_id])
        return -1;

    var buf = a.bufs[buf_id];
    var ctx = a.ctx;

    var ab = ctx.createBuffer(buf.channels, buf.numSamples, buf.sampleRate);
    if (buf.channels == 1)
    {
        ab.copyToChannel(buf.data, 0);
    }
    else
    {
        
        var left = new Float32Array(buf.numSamples);
        var right = new Float32Array(buf.numSamples);
        for (var i = 0; i < buf.numSamples; i++)
        {
            left[i] = buf.data[i * 2];
            right[i] = buf.data[i * 2 + 1];
        }
        ab.copyToChannel(left, 0);
        ab.copyToChannel(right, 1);
    }

    var source = ctx.createBufferSource();
    source.buffer = ab;
    source.loop = (loop != 0);

    var gn = ctx.createGain();
    gn.gain.value = gain;
    source.connect(gn);
    gn.connect(a.masterGain);
    source.start();

    var pbId = -1;
    for (var i = 0; i < a.playbacks.length; i++)
    {
        if (!a.playbacks[i] || !a.playbacks[i].active)
        {
            pbId = i;
            break;
        }
    }
    if (pbId < 0)
    {
        if (a.playbacks.length >= 32)
        { 
            try { source.stop(); }
            catch(e) {}
            gn.disconnect();
            return -1;
        }
        pbId = a.playbacks.length;
    }

    var entry = {source : source, gain : gn, active : true, bufferId : buf_id};
    a.playbacks[pbId] = entry;

    source.onended = function()
    {
        entry.active = false;
        gn.disconnect();
    };

    return pbId;
});

EM_JS(void, js_hw_audio_stop_playback, (int pb_id), {
    if (!Module._hw_audio)
        return;
    var a = Module._hw_audio;
    if (pb_id < 0 || pb_id >= a.playbacks.length)
        return;
    var p = a.playbacks[pb_id];
    if (!p || !p.active)
        return;
    try { p.source.stop(); }
    catch(e) {}
    p.active = false;
    p.gain.disconnect();
});

AudioContext *audio_create(void)
{
    if (!js_hw_audio_init())
    {
        fprintf(stderr, "web_audio: Web Audio API not available\n");
        return NULL;
    }

    AudioContext *ctx = (AudioContext *)calloc(1, sizeof(AudioContext));
    if (!ctx)
        return NULL;

    ctx->master_gain = 0.5f;
    ctx->playing = false;
    return ctx;
}

void audio_destroy(AudioContext *ctx)
{
    if (!ctx)
        return;
    js_hw_audio_destroy();
    free(ctx);
}

void audio_set_asset_root(AudioContext *ctx, const char *asset_root)
{
    if (!ctx)
        return;
    snprintf(ctx->asset_root, sizeof(ctx->asset_root), "%s", asset_root ? asset_root : "/");
}

void audio_start(AudioContext *ctx)
{
    if (!ctx)
        return;
    js_hw_audio_resume();
    ctx->playing = true;
}

void audio_stop(AudioContext *ctx)
{
    if (!ctx)
        return;
    js_hw_audio_suspend();
    ctx->playing = false;
}

bool audio_is_playing(AudioContext *ctx)
{
    if (!ctx)
        return false;
    return js_hw_audio_is_running() != 0;
}

void audio_set_master_gain(AudioContext *ctx, float gain)
{
    if (!ctx)
        return;
    gain = fmaxf(0.0f, fminf(1.0f, gain));
    ctx->master_gain = gain;
    js_hw_audio_set_master_gain(gain);
}

float audio_get_master_gain(AudioContext *ctx)
{
    return ctx ? ctx->master_gain : 0.0f;
}

int audio_create_oscillator(AudioContext *ctx)
{
    if (!ctx)
        return -1;
    return js_hw_audio_create_osc();
}

void audio_destroy_oscillator(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    js_hw_audio_destroy_osc(osc_id);
}

void audio_oscillator_set_type(AudioContext *ctx, int osc_id, AudioWaveType type)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    js_hw_audio_osc_set_type(osc_id, (int)type);
}

void audio_oscillator_set_frequency(AudioContext *ctx, int osc_id, float freq)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    freq = fmaxf(20.0f, fminf(20000.0f, freq));
    js_hw_audio_osc_set_freq(osc_id, freq);
}

void audio_oscillator_set_gain(AudioContext *ctx, int osc_id, float gain)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    gain = fmaxf(0.0f, fminf(1.0f, gain));
    js_hw_audio_osc_set_gain(osc_id, gain);
}

void audio_oscillator_start(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;

    if (!ctx->playing)
        audio_start(ctx);

    js_hw_audio_osc_start(osc_id);
}

void audio_oscillator_stop(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    js_hw_audio_osc_stop(osc_id);
}

void audio_oscillator_set_detune(AudioContext *ctx, int osc_id, float cents)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    js_hw_audio_osc_set_detune(osc_id, cents);
}

int audio_create_buffer(AudioContext *ctx, int sample_rate, int channels,
                        int num_samples)
{
    if (!ctx || sample_rate <= 0 || channels < 1 || channels > 2 ||
        num_samples <= 0)
        return -1;
    return js_hw_audio_create_buf(sample_rate, channels, num_samples);
}

void audio_buffer_set_data(AudioContext *ctx, int buffer_id,
                           const float *samples, int offset, int count)
{
    if (!ctx || buffer_id < 0 || buffer_id >= AUDIO_MAX_BUFFERS || !samples)
        return;
    js_hw_audio_buf_set_data(buffer_id, samples, offset, count);
}

void audio_destroy_buffer(AudioContext *ctx, int buffer_id)
{
    if (!ctx || buffer_id < 0 || buffer_id >= AUDIO_MAX_BUFFERS)
        return;
    js_hw_audio_destroy_buf(buffer_id);
}

int audio_play_buffer(AudioContext *ctx, int buffer_id, bool loop, float gain)
{
    if (!ctx || buffer_id < 0 || buffer_id >= AUDIO_MAX_BUFFERS)
        return -1;

    if (!ctx->playing)
        audio_start(ctx);

    gain = fmaxf(0.0f, fminf(1.0f, gain));
    return js_hw_audio_play_buf(buffer_id, loop ? 1 : 0, gain);
}

void audio_stop_buffer(AudioContext *ctx, int playback_id)
{
    if (!ctx || playback_id < 0 || playback_id >= MAX_BUFFER_PLAYBACKS)
        return;
    js_hw_audio_stop_playback(playback_id);
}

int audio_load_buffer(AudioContext *ctx, const char *path)
{
    size_t size = 0;
    uint8_t *data;
    int buffer_id;

    if (!ctx)
        return -1;

    ctx->error_msg[0] = '\0';

    data = audio_read_asset_file(ctx, path, &size);
    if (!data)
        return -1;
    buffer_id = audio_load_buffer_from_memory(ctx, data, size);
    free(data);
    return buffer_id;
}

int audio_load_buffer_from_memory(AudioContext *ctx, const uint8_t *data, size_t size)
{
    if (!ctx)
        return -1;

    AudioDecodedData decoded;
    ctx->error_msg[0] = '\0';

    if (!audio_decode_memory(data, size, &decoded, ctx->error_msg, sizeof(ctx->error_msg)))
        return -1;

    int buffer_id = audio_create_buffer(ctx, decoded.sample_rate, decoded.channels, decoded.frame_count);
    if (buffer_id >= 0)
        audio_buffer_set_data(ctx, buffer_id, decoded.samples, 0, decoded.frame_count * decoded.channels);
    else
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "audio: no free buffer slot for buffer");

    audio_decoded_data_free(&decoded);
    return buffer_id;
}

void audio_oscillator_set_envelope(AudioContext *ctx, int osc_id,
                                   float attack, float decay,
                                   float sustain, float release)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;

    attack = fmaxf(0.0f, attack);
    decay = fmaxf(0.0f, decay);
    sustain = fmaxf(0.0f, fminf(1.0f, sustain));
    release = fmaxf(0.0f, release);

    js_hw_audio_osc_set_env(osc_id, attack, decay, sustain, release);
}

void audio_oscillator_note_on(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;

    if (!ctx->playing)
        audio_start(ctx);

    js_hw_audio_osc_note_on(osc_id);
}

void audio_oscillator_note_off(AudioContext *ctx, int osc_id)
{
    if (!ctx || osc_id < 0 || osc_id >= AUDIO_MAX_OSCILLATORS)
        return;
    js_hw_audio_osc_note_off(osc_id);
}

float audio_midi_to_freq(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

const char *audio_get_error(AudioContext *ctx)
{
    return ctx ? ctx->error_msg : "Invalid context";
}