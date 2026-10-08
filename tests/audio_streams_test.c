#include "audio/audio_decoder.h"
#include "audio/audio_streams.h"
#include "file/file_wrapper.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_ring_wraps(void)
{
    AudioRing ring;
    assert(audio_ring_init(&ring, 5, 2));
    float in[8] = {1, 2, 3, 4, 5, 6, 7, 8}, out[10];
    assert(audio_ring_write(&ring, in, 4) == 4);
    assert(audio_ring_read(&ring, out, 3) == 3 && out[0] == 1 && out[5] == 6);
    
    assert(audio_ring_write(&ring, in, 4) == 4);
    assert(audio_ring_write(&ring, in, 4) == 0);
    assert(audio_ring_queued(&ring) == 5);
    assert(audio_ring_read(&ring, out, 5) == 5);
    assert(out[0] == 7 && out[1] == 8 && out[2] == 1 && out[9] == 8);
    audio_ring_free(&ring);
}

static void test_output_mix_and_underruns(void)
{
    AudioStreams streams;
    assert(audio_streams_init(&streams, 48000));
    int mono = audio_streams_open(&streams, false, 1, 20.0);
    assert(mono == 0);
    
    assert(audio_streams_output_wanted(&streams, mono) == 960);

    float out[2 * 256];
    memset(out, 0, sizeof(out));
    audio_streams_mix(&streams, out, 256, 2);
    AudioStreamStats stats;
    assert(audio_streams_stats(&streams, false, mono, &stats));
    assert(stats.glitches == 0 && stats.frames == 256 && out[0] == 0.0f);

    float chunk[256];
    for (int i = 0; i < 256; i++)
        chunk[i] = 0.25f;
    assert(audio_streams_output_write(&streams, mono, chunk, 256) == 256);
    assert(audio_streams_output_wanted(&streams, mono) == 960 - 256);
    
    for (int i = 0; i < 2 * 256; i++)
        out[i] = 0.5f;
    audio_streams_mix(&streams, out, 256, 2);
    assert(fabsf(out[0] - 0.75f) < 1e-6f && fabsf(out[511] - 0.75f) < 1e-6f);
    
    audio_streams_mix(&streams, out, 128, 2);
    assert(audio_streams_stats(&streams, false, mono, &stats) && stats.glitches == 1);

    assert(audio_streams_take_wake(&streams));
    assert(!audio_streams_take_wake(&streams));
    audio_streams_clear_wake(&streams);
    assert(audio_streams_take_wake(&streams));

    audio_streams_close(&streams, false, mono);
    assert(!audio_streams_any_open(&streams));
    audio_streams_destroy(&streams);
}

static void test_adaptive_output(void)
{
    AudioStreams streams;
    assert(audio_streams_init(&streams, 48000));
    int id = audio_streams_open(&streams, false, 1, 0.0);
    assert(id >= 0);
    const int start = 960; 
    float chunk[AUDIO_STREAM_CHUNK_FRAMES] = {0};
    float out[2 * AUDIO_STREAM_CHUNK_FRAMES];
    assert(audio_streams_output_wanted(&streams, id) == start);
    
    for (int i = 0; i < 48000 * AUDIO_STREAM_ADAPTIVE_WARMUP_SECONDS / AUDIO_STREAM_CHUNK_FRAMES + 1; i++)
    {
        audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
        audio_streams_output_wanted(&streams, id);
    }
    assert(audio_streams_output_wanted(&streams, id) == start);
    
    assert(audio_streams_output_write(&streams, id, chunk, AUDIO_STREAM_CHUNK_FRAMES) == AUDIO_STREAM_CHUNK_FRAMES);
    audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
    audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
    AudioStreamStats stats;
    assert(audio_streams_stats(&streams, false, id, &stats) && stats.glitches >= 1);
    
    int grown = start + AUDIO_STREAM_CHUNK_FRAMES + AUDIO_STREAM_ADAPTIVE_GROW * AUDIO_STREAM_CHUNK_FRAMES;
    assert(audio_streams_output_wanted(&streams, id) == grown);
    assert(audio_streams_stats(&streams, false, id, &stats) && stats.latency_frames == grown);
    uint64_t glitches = stats.glitches;

    int steady_chunks = 48000 * AUDIO_STREAM_ADAPTIVE_WINDOW_SECONDS / AUDIO_STREAM_CHUNK_FRAMES + 2;
    for (int i = 0; i < steady_chunks; i++)
    {
        int wanted = audio_streams_output_wanted(&streams, id);
        while (wanted > 0)
        {
            int n = wanted < AUDIO_STREAM_CHUNK_FRAMES ? wanted : AUDIO_STREAM_CHUNK_FRAMES;
            audio_streams_output_write(&streams, id, chunk, n);
            wanted -= n;
        }
        audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
    }
    assert(audio_streams_stats(&streams, false, id, &stats) && stats.glitches == glitches);

    assert(stats.latency_frames == grown - 2 * AUDIO_STREAM_CHUNK_FRAMES);
    
    for (int i = 0; i < 200; i++)
    {
        
        audio_streams_output_write(&streams, id, chunk, AUDIO_STREAM_CHUNK_FRAMES);
        while (audio_ring_queued(&streams.outputs[id].ring) > 0)
            audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
        audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
        audio_streams_output_wanted(&streams, id);
    }
    assert(audio_streams_stats(&streams, false, id, &stats));
    assert(stats.latency_frames == (int)ceil(AUDIO_STREAM_ADAPTIVE_MAX_MS * 48000 / 1000.0));
    
    uint64_t before = stats.glitches;
    audio_streams_output_write(&streams, id, chunk, AUDIO_STREAM_CHUNK_FRAMES);
    for (int i = 0; i < 10; i++)
        audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
    assert(audio_streams_stats(&streams, false, id, &stats) && stats.glitches == before + 1);
    
    int fixed = audio_streams_open(&streams, false, 1, 10.0);
    audio_streams_output_write(&streams, fixed, chunk, AUDIO_STREAM_CHUNK_FRAMES);
    audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
    audio_streams_mix(&streams, out, AUDIO_STREAM_CHUNK_FRAMES, 2);
    audio_streams_output_wanted(&streams, fixed);
    assert(audio_streams_stats(&streams, false, fixed, &stats) && stats.latency_frames == 2 * AUDIO_STREAM_CHUNK_FRAMES);
    audio_streams_destroy(&streams);
}

static void test_input_capture(void)
{
    AudioStreams streams;
    assert(audio_streams_init(&streams, 48000));
    int input = audio_streams_open(&streams, true, 1, 10.0);
    assert(input >= 0);
    
    float stereo[2 * 512];
    for (int i = 0; i < 512; i++)
    {
        stereo[i * 2] = 0.2f;
        stereo[i * 2 + 1] = 0.6f;
    }
    audio_streams_capture(&streams, stereo, 512, 2);
    assert(audio_streams_input_available(&streams, input) == 512);
    float mono[512];
    assert(audio_streams_input_read(&streams, input, mono, 512) == 512);
    assert(fabsf(mono[0] - 0.4f) < 1e-6f && fabsf(mono[511] - 0.4f) < 1e-6f);
    
    for (int i = 0; i < 20; i++)
        audio_streams_capture(&streams, stereo, 512, 2);
    AudioStreamStats stats;
    assert(audio_streams_stats(&streams, true, input, &stats) && stats.glitches > 0);
    assert(stats.frames == 21 * 512);
    audio_streams_destroy(&streams);
}

static uint8_t *make_wav(int rate, int channels, int frames, size_t *size)
{
    size_t data = (size_t)frames * (size_t)channels * 2;
    uint8_t *wav = (uint8_t *)calloc(1, 44 + data);
    memcpy(wav, "RIFF", 4);
    uint32_t value = (uint32_t)(36 + data);
    memcpy(wav + 4, &value, 4);
    memcpy(wav + 8, "WAVEfmt ", 8);
    value = 16;
    memcpy(wav + 16, &value, 4);
    uint16_t short_value = 1;
    memcpy(wav + 20, &short_value, 2);
    short_value = (uint16_t)channels;
    memcpy(wav + 22, &short_value, 2);
    value = (uint32_t)rate;
    memcpy(wav + 24, &value, 4);
    value = (uint32_t)(rate * channels * 2);
    memcpy(wav + 28, &value, 4);
    short_value = (uint16_t)(channels * 2);
    memcpy(wav + 32, &short_value, 2);
    short_value = 16;
    memcpy(wav + 34, &short_value, 2);
    memcpy(wav + 36, "data", 4);
    value = (uint32_t)data;
    memcpy(wav + 40, &value, 4);
    int16_t *samples = (int16_t *)(wav + 44);
    for (int f = 0; f < frames; f++)
        for (int c = 0; c < channels; c++)
            samples[f * channels + c] = (int16_t)(sin(2 * M_PI * 440.0 * f / rate) * 12000);
    *size = 44 + data;
    return wav;
}

static int64_t read_all(AudioStreamDecoder *decoder, int channels)
{
    float block[2 * 1000];
    int64_t total = 0;
    for (;;)
    {
        int got = audio_stream_decoder_read(decoder, block, 1000, NULL, 0);
        assert(got >= 0);
        if (got == 0)
            break;
        total += got;
    }
    (void)channels;
    return total;
}

static void test_decoder_memory_resamples_and_seeks(void)
{
    size_t size = 0;
    uint8_t *wav = make_wav(22050, 1, 22050, &size); 
    char error[256] = "";
    AudioStreamDecoder *decoder = audio_stream_decoder_open_memory(wav, size, 44100, 2, error, sizeof(error));
    assert(decoder);
    AudioStreamDecoderInfo info;
    audio_stream_decoder_info(decoder, &info);
    assert(info.ready && info.seekable && !info.ended);
    assert(info.sample_rate == 44100 && info.channels == 2);
    assert(info.source_sample_rate == 22050 && info.source_channels == 1);
    assert(info.length_frames > 44000 && info.length_frames < 44200);
    int64_t frames = read_all(decoder, 2);
    assert(frames > 44000 && frames < 44200);
    audio_stream_decoder_info(decoder, &info);
    assert(info.ended);
    
    assert(audio_stream_decoder_seek(decoder, 22050, error, sizeof(error)));
    audio_stream_decoder_info(decoder, &info);
    assert(!info.ended && info.position_frames == 22050);
    int64_t rest = read_all(decoder, 2);
    assert(rest > 21900 && rest < 22200);
    audio_stream_decoder_close(decoder);

    decoder = audio_stream_decoder_open_memory(wav, size, 0, 1, error, sizeof(error));
    assert(decoder);
    audio_stream_decoder_info(decoder, &info);
    assert(info.sample_rate == 22050 && info.channels == 1 && read_all(decoder, 1) == 22050);
    audio_stream_decoder_close(decoder);

    assert(!audio_stream_decoder_open_memory((const uint8_t *)"hello", 5, 0, 2, error, sizeof(error)));
    assert(error[0]);
    free(wav);
}

static void test_decoder_push_waits_for_bytes(void)
{
    size_t size = 0;
    uint8_t *wav = make_wav(48000, 2, 48000, &size); 
    AudioStreamDecoder *decoder = audio_stream_decoder_create_push(0, 2);
    assert(decoder);
    float block[2 * 512];
    AudioStreamDecoderInfo info;
    
    assert(audio_stream_decoder_feed(decoder, wav, 1000, false, NULL, 0));
    assert(audio_stream_decoder_read(decoder, block, 512, NULL, 0) == 0);
    audio_stream_decoder_info(decoder, &info);
    assert(!info.ready && info.needs_data && !info.seekable && info.length_frames == -1);
    
    int64_t total = 0;
    size_t fed = 1000;
    while (fed < size)
    {
        size_t piece = size - fed < 3000 ? size - fed : 3000;
        assert(audio_stream_decoder_feed(decoder, wav + fed, piece, fed + piece == size, NULL, 0));
        fed += piece;
        int got;
        while ((got = audio_stream_decoder_read(decoder, block, 512, NULL, 0)) > 0)
            total += got;
        assert(got == 0);
    }
    audio_stream_decoder_info(decoder, &info);
    assert(info.ready && info.ended && info.sample_rate == 48000);
    assert(total == 48000);
    
    assert(!audio_stream_decoder_seek(decoder, 0, NULL, 0));
    audio_stream_decoder_close(decoder);
    free(wav);
}

static void test_decoder_file_reads_in_blocks(void)
{
    char dir[] = "/tmp/budo_audio_streams_XXXXXX";
    assert(mkdtemp(dir));
    char path[512];
    snprintf(path, sizeof(path), "%s/tone.wav", dir);
    size_t size = 0;
    uint8_t *wav = make_wav(44100, 2, 44100 * 3, &size);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(wav, 1, size, file) == size);
    fclose(file);
    FileContext *files = file_create(dir);
    assert(files);
    FileNativeReference ref;
    assert(file_native_open(files, "assets/tone.wav", &ref));
    
    uint8_t block[16];
    assert(file_native_read(&ref, 44, block, sizeof(block)) == 16 && memcmp(block, wav + 44, 16) == 0);
    assert(file_native_read(&ref, size, block, sizeof(block)) == 0);
    char error[256] = "";
    AudioStreamDecoder *decoder = audio_stream_decoder_open_file(&ref, 0, 2, error, sizeof(error));
    assert(decoder);
    AudioStreamDecoderInfo info;
    audio_stream_decoder_info(decoder, &info);
    assert(info.length_frames == 44100 * 3 && info.seekable);
    assert(audio_stream_decoder_seek(decoder, 44100 * 2, error, sizeof(error)));
    assert(read_all(decoder, 2) == 44100);
    audio_stream_decoder_close(decoder);
    file_destroy(files);
    unlink(path);
    rmdir(dir);
    free(wav);
}

int main(void)
{
    test_ring_wraps();
    test_output_mix_and_underruns();
    test_adaptive_output();
    test_input_capture();
    test_decoder_memory_resamples_and_seeks();
    test_decoder_push_waits_for_bytes();
    test_decoder_file_reads_in_blocks();
    puts("audio_streams_test: ok");
    return 0;
}