#include "file/file_wrapper.h"
#include "video/video_demux.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_tracks_and_packets(VideoDemux *demux)
{
    int video = 0, audio = 0, subtitle = 0, english = 0, french = 0;
    int default_forced = 0;
    int counts[64] = {0};
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        VideoTrack track;
        assert(video_demux_track(demux, i, &track));
        assert(track.time_base_num > 0 && track.time_base_den > 0);
        if (track.type == VIDEO_TRACK_VIDEO && strcmp(track.codec, "h264") == 0)
        {
            video++;
            assert((track.metadata & VIDEO_TRACK_HAS_DIMENSIONS) != 0);
            assert((track.metadata & VIDEO_TRACK_HAS_START_TIME) != 0);
            assert((track.metadata & VIDEO_TRACK_HAS_DURATION) != 0);
            assert(track.width == 64 && track.height == 48);
            assert(track.duration > 0);
            assert((track.metadata & VIDEO_TRACK_HAS_FRAME_RATE) != 0);
            assert(track.frame_rate_num == 8 && track.frame_rate_den == 1);
            assert((track.metadata & VIDEO_TRACK_HAS_SAMPLE_ASPECT_RATIO) != 0);
            assert(track.sample_aspect_ratio_num == 1 &&
                   track.sample_aspect_ratio_den == 1);
            assert((track.metadata & VIDEO_TRACK_HAS_ROTATION) != 0);
            assert(track.rotation_degrees == 0);
            assert(track.color_primaries == VIDEO_COLOR_BT709);
            assert(track.color_transfer == VIDEO_COLOR_BT709);
            assert(track.color_matrix == VIDEO_COLOR_BT709);
            assert(track.color_range == VIDEO_COLOR_RANGE_LIMITED);
        }
        if (track.type == VIDEO_TRACK_AUDIO && strcmp(track.codec, "aac") == 0)
        {
            audio++;
            assert(track.sample_rate == 48000);
            assert(track.channels == 1 || track.channels == 2);
            assert(track.channel_layout[0]);
        }
        if (track.type == VIDEO_TRACK_SUBTITLE && strcmp(track.codec, "mov_text") == 0)
        {
            subtitle++;
            if (strcmp(track.language, "eng") == 0)
            {
                english++;
                assert((track.metadata & VIDEO_TRACK_HAS_LANGUAGE) != 0);
                assert((track.metadata & VIDEO_TRACK_HAS_TITLE) != 0);
                assert(strcmp(track.title, "English captions") == 0);
                default_forced = track.disposition_default && track.disposition_forced;
            }
            if (strcmp(track.language, "fra") == 0)
            {
                french++;
                assert(strcmp(track.title, "French captions") == 0);
                assert(!track.disposition_default && !track.disposition_forced);
            }
        }
    }
    assert(video == 1 && audio == 2 && subtitle == 2 && english == 1 && french == 1);
    assert(default_forced);
    VideoTrack track;
    assert(!video_demux_track(demux, -1, &track));
    assert(!video_demux_track(demux, video_demux_track_count(demux), &track));
    VideoPacket packet;
    char error[256];
    int status;
    while ((status = video_demux_next(demux, &packet, error, sizeof(error))) == 1)
    {
        assert(packet.track >= 0 && packet.track < video_demux_track_count(demux));
        assert(packet.data || packet.size == 0);
        counts[packet.track]++;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && error[0] == '\0');
    for (int i = 0; i < video_demux_track_count(demux); ++i)
        assert(counts[i] > 0);
    assert(video_demux_next(demux, &packet, error, sizeof(error)) == 0);
}

static void check_decoded_frames(VideoDemux *demux)
{
    char error[256];
    VideoFrame frame;
    assert(video_demux_seek_time(demux, 0, &frame, error, sizeof(error)) == -1);
    assert(error[0]);
    assert(video_demux_seek_frame(demux, 0, &frame, error, sizeof(error)) == -1);
    assert(error[0]);
    assert(!video_demux_decode_open(demux, 1, error, sizeof(error)));
    assert(error[0]);
    assert(video_demux_decode_open(demux, 0, error, sizeof(error)));
    VideoPacket packet;
    assert(video_demux_next(demux, &packet, error, sizeof(error)) == -1);
    assert(error[0]);
    int64_t timestamps[8];
    int count = 0;
    int64_t previous_pts = INT64_MIN;
    int status;
    while ((status = video_demux_decode_next(demux, &frame, error, sizeof(error))) == 1)
    {
        assert(frame.width == 64 && frame.height == 48);
        assert(frame.stride == frame.width * 4);
        assert(frame.size == (size_t)frame.stride * frame.height);
        assert(frame.rgba && frame.time_base_num > 0 && frame.time_base_den > 0);
        assert(frame.sample_aspect_ratio_num == 1 &&
               frame.sample_aspect_ratio_den == 1);
        assert(frame.color_primaries == VIDEO_COLOR_BT709);
        assert(frame.color_transfer == VIDEO_COLOR_BT709);
        assert(frame.color_matrix == VIDEO_COLOR_BT709);
        assert(frame.color_range == VIDEO_COLOR_RANGE_LIMITED);
        int has_nonzero_pixel = 0;
        for (size_t i = 0; i < frame.size; ++i)
            has_nonzero_pixel |= frame.rgba[i] != 0;
        assert(has_nonzero_pixel);
        assert(frame.pts != INT64_MIN && frame.pts > previous_pts);
        previous_pts = frame.pts;
        assert(count < (int)(sizeof(timestamps) / sizeof(timestamps[0])));
        timestamps[count] = frame.pts;
        count++;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && error[0] == '\0');
    assert(count == 8);
    assert(video_demux_decode_next(demux, &frame, error, sizeof(error)) == 0);

    assert(video_demux_seek_time(demux, timestamps[5], &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[5]);
    assert(video_demux_decode_next(demux, &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[6]);
    assert(video_demux_seek_time(demux, timestamps[2], &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[2]);
    assert(video_demux_seek_time(demux, timestamps[3] + 1, &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[4]);
    assert(video_demux_seek_time(demux, timestamps[7] + 1, &frame, error, sizeof(error)) == 0);
    assert(error[0] == '\0');
    assert(video_demux_seek_time(demux, timestamps[0], &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[0]);

    assert(video_demux_seek_frame(demux, 5, &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[5]);
    assert(video_demux_decode_next(demux, &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[6]);
    assert(video_demux_seek_frame(demux, 2, &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[2]);
    assert(video_demux_seek_frame(demux, 8, &frame, error, sizeof(error)) == 0);
    assert(error[0] == '\0');
    assert(video_demux_seek_frame(demux, 0, &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[0]);
}

static uint64_t frame_hash(const VideoFrame *frame)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < frame->size; ++i)
    {
        hash ^= frame->rgba[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void check_long_gop_seeks(FileContext *files)
{
    enum { FRAME_COUNT = 180 };
    int64_t timestamps[FRAME_COUNT];
    uint64_t hashes[FRAME_COUNT];
    char error[256];
    VideoDemux *demux = video_demux_open_file(
        files, "assets/long-gop.mp4", error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);

    int video_track = -1;
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        VideoTrack track;
        assert(video_demux_track(demux, i, &track));
        if (track.type == VIDEO_TRACK_VIDEO)
            video_track = i;
    }
    assert(video_track >= 0);
    assert(video_demux_decode_open(demux, video_track, error, sizeof(error)));

    VideoFrame frame;
    int count = 0;
    int status;
    while ((status = video_demux_decode_next(demux, &frame,
                                              error, sizeof(error))) == 1)
    {
        assert(count < FRAME_COUNT);
        assert(frame.pts != INT64_MIN);
        if (count)
            assert(frame.pts > timestamps[count - 1]);
        timestamps[count] = frame.pts;
        hashes[count] = frame_hash(&frame);
        count++;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && count == FRAME_COUNT);
    assert(hashes[0] != hashes[FRAME_COUNT - 1]);
    video_demux_close(demux);

    static const int seek_order[] = {
        179, 61, 29, 120, 1, 118, 60, 0, 90, 59, 121,
    };
    demux = video_demux_open_file(files, "assets/long-gop.mp4",
                                  error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);
    assert(video_demux_decode_open(demux, video_track, error, sizeof(error)));
    for (size_t i = 0; i < sizeof(seek_order) / sizeof(seek_order[0]); ++i)
    {
        int ordinal = seek_order[i];
        assert(video_demux_seek_time(demux, timestamps[ordinal],
                                     &frame, error, sizeof(error)) == 1);
        assert(frame.pts == timestamps[ordinal]);
        assert(frame_hash(&frame) == hashes[ordinal]);
        if (ordinal + 1 < FRAME_COUNT)
        {
            assert(video_demux_decode_next(demux, &frame,
                                           error, sizeof(error)) == 1);
            assert(frame.pts == timestamps[ordinal + 1]);
            assert(frame_hash(&frame) == hashes[ordinal + 1]);
        }
    }
    assert(video_demux_seek_time(demux, timestamps[58] + 1,
                                 &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[59]);
    assert(frame_hash(&frame) == hashes[59]);
    assert(video_demux_seek_time(demux, timestamps[FRAME_COUNT - 1] + 1,
                                 &frame, error, sizeof(error)) == 0);
    assert(video_demux_seek_time(demux, timestamps[60],
                                 &frame, error, sizeof(error)) == 1);
    assert(frame.pts == timestamps[60]);
    assert(frame_hash(&frame) == hashes[60]);
    video_demux_close(demux);

    demux = video_demux_open_file(files, "assets/long-gop.mp4",
                                  error, sizeof(error));
    assert(demux);
    int keyframes = 0;
    int reordered_packets = 0;
    VideoPacket packet;
    while ((status = video_demux_next(demux, &packet,
                                      error, sizeof(error))) == 1)
    {
        if (packet.track != video_track)
            continue;
        keyframes += packet.keyframe != 0;
        reordered_packets += packet.pts != INT64_MIN &&
                             packet.dts != INT64_MIN &&
                             packet.pts != packet.dts;
    }
    assert(status == 0 && keyframes == 3 && reordered_packets > 0);
    video_demux_close(demux);
}

static void check_audio_blocks(const uint8_t *bytes, size_t size, int track)
{
    char error[256];
    VideoDemux *demux = video_demux_open_memory(bytes, size, error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);
    VideoAudioBlock block;
    assert(video_demux_audio_next(demux, &block, error, sizeof(error)) == -1);
    assert(error[0]);
    assert(!video_demux_audio_open(demux, -1, error, sizeof(error)));
    assert(error[0]);
    assert(video_demux_audio_open(demux, track, error, sizeof(error)));
    assert(error[0] == '\0');
    VideoPacket packet;
    assert(video_demux_next(demux, &packet, error, sizeof(error)) == -1);
    assert(error[0]);

    size_t total_frames = 0;
    int64_t previous_pts = INT64_MIN;
    int status;
    while ((status = video_demux_audio_next(demux, &block, error, sizeof(error))) == 1)
    {
        assert(block.samples && block.frame_count > 0);
        assert(block.sample_rate == 48000 && block.channels > 0);
        assert(block.frame_count <= 1024u * 1024u / (size_t)block.channels);
        assert(block.pts_us != INT64_MIN && block.pts_us > previous_pts);
        assert(block.duration_us > 0);
        previous_pts = block.pts_us;
        size_t values = block.frame_count * (size_t)block.channels;
        int has_signal = 0;
        for (size_t i = 0; i < values; ++i)
        {
            assert(isfinite(block.samples[i]));
            has_signal |= fabsf(block.samples[i]) > 0.001f;
        }
        assert(has_signal);
        total_frames += block.frame_count;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && error[0] == '\0');
    assert(total_frames >= 48000 && total_frames <= 48000 + 2048);
    assert(video_demux_audio_next(demux, &block, error, sizeof(error)) == 0);
    video_demux_close(demux);
}

static void check_subtitle_cues(const uint8_t *bytes, size_t size, int track,
                                const char *expected_text)
{
    char error[256];
    VideoDemux *demux = video_demux_open_memory(bytes, size, error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);
    assert(!video_demux_subtitle_open(demux, -1, error, sizeof(error)));
    assert(error[0]);
    assert(video_demux_subtitle_open(demux, track, error, sizeof(error)));
    VideoSubtitle subtitle;
    int status = video_demux_subtitle_next(demux, &subtitle, error, sizeof(error));
    if (status != 1)
        fprintf(stderr, "subtitle cue status %d: %s\n", status, error);
    assert(status == 1);
    size_t expected_size = strlen(expected_text);
    assert(subtitle.text_size == expected_size &&
           memcmp(subtitle.text, expected_text, expected_size) == 0);
    assert(subtitle.start_time_us == 0);
    assert(subtitle.end_time_us == 800000);
    VideoPacket packet;
    assert(video_demux_next(demux, &packet, error, sizeof(error)) == -1);
    assert(error[0]);
    assert(video_demux_subtitle_next(demux, &subtitle, error, sizeof(error)) == 0);
    assert(error[0] == '\0');
    video_demux_close(demux);
}

static void check_writer(FileContext *files)
{
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    char error[256];
    VideoTrack source_subtitles[64];
    int source_subtitle_tracks[64];
    VideoWriterSubtitleConfig subtitles[64] = {0};
    size_t subtitle_count = 0;
    VideoDemux *source = video_demux_open_file(files, "assets/clip.mp4",
                                                error, sizeof(error));
    if (!source)
        fprintf(stderr, "%s\n", error);
    assert(source);
    for (int i = 0; i < video_demux_track_count(source); ++i)
    {
        VideoTrack track;
        assert(video_demux_track(source, i, &track));
        if (track.type != VIDEO_TRACK_SUBTITLE)
            continue;
        assert(subtitle_count < sizeof(source_subtitles) / sizeof(source_subtitles[0]));
        assert(strcmp(track.codec, "mov_text") == 0);
        source_subtitles[subtitle_count] = track;
        source_subtitle_tracks[subtitle_count] = i;
        subtitles[subtitle_count].language =
            (track.metadata & VIDEO_TRACK_HAS_LANGUAGE)
                ? source_subtitles[subtitle_count].language : NULL;
        subtitles[subtitle_count].title =
            (track.metadata & VIDEO_TRACK_HAS_TITLE)
                ? source_subtitles[subtitle_count].title : NULL;
        subtitles[subtitle_count].disposition_default = track.disposition_default;
        subtitles[subtitle_count].disposition_forced = track.disposition_forced;
        subtitle_count++;
    }
    video_demux_close(source);
    assert(subtitle_count == 2);
    VideoWriterConfig config = {
        .width = 64,
        .height = 48,
        .frame_rate_num = 30,
        .frame_rate_den = 1,
        .video_bit_rate = 300000,
        .audio_sample_rate = 48000,
        .audio_channels = 2,
        .audio_bit_rate = 128000,
        .subtitles = subtitles,
        .subtitle_count = subtitle_count,
    };
    VideoWriter *aborted = video_writer_open_file(files, "files/aborted.mp4",
                                                   &config, error, sizeof(error));
    if (!aborted)
        fprintf(stderr, "%s\n", error);
    assert(aborted);
    video_writer_close(aborted);
    assert(!file_is_file_at(files, FILE_ROOT_FILES, "aborted.mp4"));

    VideoWriter *writer = video_writer_open_file(files, "files/export.mp4",
                                                  &config, error, sizeof(error));
    if (!writer)
        fprintf(stderr, "%s\n", error);
    assert(writer);
    uint8_t rgba[64 * 48 * 4];
    float audio[1600 * 2];
    for (size_t i = 0; i < subtitle_count; ++i)
    {
        source = video_demux_open_file(files, "assets/clip.mp4",
                                       error, sizeof(error));
        if (!source)
            fprintf(stderr, "%s\n", error);
        assert(source);
        assert(video_demux_subtitle_open(source, source_subtitle_tracks[i],
                                         error, sizeof(error)));
        VideoSubtitle cue;
        int cue_status;
        while ((cue_status = video_demux_subtitle_next(source, &cue,
                                                        error, sizeof(error))) == 1)
            assert(video_writer_write_subtitle(writer, i, cue.text, cue.text_size,
                                               cue.start_time_us, cue.end_time_us,
                                               error, sizeof(error)));
        if (cue_status < 0)
            fprintf(stderr, "%s\n", error);
        assert(cue_status == 0 && error[0] == '\0');
        video_demux_close(source);
    }
    assert(!video_writer_write_subtitle(writer, 0, "bad", 3, 900000, 800000,
                                        error, sizeof(error)));
    assert(error[0]);
    assert(!video_writer_write_subtitle(writer, 0, "bad", 3, 1000, 2001,
                                        error, sizeof(error)));
    assert(error[0]);
    const char invalid_utf8[] = {(char)0xc0, (char)0xaf};
    assert(!video_writer_write_subtitle(writer, 0, invalid_utf8,
                                        sizeof(invalid_utf8), 0, 800000,
                                        error, sizeof(error)));
    assert(error[0]);
    for (int frame = 0; frame < 30; ++frame)
    {
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
            {
                size_t pixel = ((size_t)y * 64 + (size_t)x) * 4;
                rgba[pixel] = (uint8_t)(x * 4);
                rgba[pixel + 1] = (uint8_t)(y * 5);
                rgba[pixel + 2] = (uint8_t)(frame * 7);
                rgba[pixel + 3] = 255;
            }
        int64_t frame_pts = (int64_t)frame * 1000000 / 30;
        assert(video_writer_write_frame(writer, rgba, sizeof(rgba), frame_pts,
                                        error, sizeof(error)));
        for (int sample = 0; sample < 1600; ++sample)
        {
            float value = sinf((float)(frame * 1600 + sample) * 0.0576f) * 0.25f;
            audio[sample * 2] = value;
            audio[sample * 2 + 1] = value;
        }
        int64_t audio_pts = (int64_t)frame * 1600 * 1000000 / 48000;
        if (frame == 1)
            assert(!video_writer_write_audio(writer, audio, 1600, audio_pts + 20000,
                                              error, sizeof(error)));
        assert(video_writer_write_audio(writer, audio, 1600, audio_pts,
                                        error, sizeof(error)));
    }
    if (!video_writer_finish(writer, error, sizeof(error)))
        fprintf(stderr, "%s\n", error);
    assert(error[0] == '\0');

    VideoDemux *demux = video_demux_open_file(files, "files/export.mp4",
                                               error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);
    int video_track = -1;
    int audio_track = -1;
    int subtitle_tracks[2];
    int subtitle_track_count = 0;
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        VideoTrack track;
        assert(video_demux_track(demux, i, &track));
        if (track.type == VIDEO_TRACK_VIDEO)
            video_track = i;
        if (track.type == VIDEO_TRACK_AUDIO)
            audio_track = i;
        if (track.type == VIDEO_TRACK_SUBTITLE)
        {
            assert(subtitle_track_count < 2);
            assert(strcmp(track.codec, "mov_text") == 0);
            assert(subtitle_track_count < (int)subtitle_count);
            const VideoTrack *source_track =
                &source_subtitles[subtitle_track_count];
            assert(strcmp(track.language, source_track->language) == 0);
            assert(strcmp(track.title, source_track->title) == 0);
            assert(track.disposition_default == source_track->disposition_default);
            assert(track.disposition_forced == source_track->disposition_forced);
            subtitle_tracks[subtitle_track_count++] = i;
        }
    }
    assert(video_track >= 0 && audio_track >= 0 &&
           subtitle_track_count == (int)subtitle_count);
    assert(video_demux_decode_open(demux, video_track, error, sizeof(error)));
    VideoFrame decoded;
    int frame_count = 0;
    int status;
    while ((status = video_demux_decode_next(demux, &decoded,
                                               error, sizeof(error))) == 1)
        frame_count++;
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && frame_count == 30);
    video_demux_close(demux);

    demux = video_demux_open_file(files, "files/export.mp4",
                                  error, sizeof(error));
    assert(demux);
    assert(video_demux_audio_open(demux, audio_track, error, sizeof(error)));
    VideoAudioBlock block;
    size_t audio_frames = 0;
    while ((status = video_demux_audio_next(demux, &block,
                                              error, sizeof(error))) == 1)
        audio_frames += block.frame_count;
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && audio_frames >= 48000);
    video_demux_close(demux);

    for (int i = 0; i < subtitle_track_count; ++i)
    {
        demux = video_demux_open_file(files, "files/export.mp4",
                                      error, sizeof(error));
        assert(demux);
        assert(video_demux_subtitle_open(demux, subtitle_tracks[i],
                                         error, sizeof(error)));
        VideoSubtitle subtitle;
        assert(video_demux_subtitle_next(demux, &subtitle,
                                         error, sizeof(error)) == 1);
        const char *expected_text = i == 0 ? "Hello" : "Bonjour";
        size_t expected_size = strlen(expected_text);
        assert(subtitle.text_size == expected_size &&
               memcmp(subtitle.text, expected_text, expected_size) == 0);
        assert(subtitle.start_time_us == 0);
        assert(subtitle.end_time_us == 800000);
        assert(video_demux_subtitle_next(demux, &subtitle,
                                         error, sizeof(error)) == 0);
        video_demux_close(demux);
    }
#else
    (void)files;
#endif
}

static void check_unsupported_color(FileContext *files)
{
    char error[256];
    VideoDemux *demux = video_demux_open_file(
        files, "assets/unsupported-color.mp4", error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);

    int video_track = -1;
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        VideoTrack track;
        assert(video_demux_track(demux, i, &track));
        if (track.type == VIDEO_TRACK_VIDEO)
        {
            video_track = i;
            assert(track.color_primaries == VIDEO_COLOR_UNSUPPORTED);
            assert(track.color_transfer == VIDEO_COLOR_UNSUPPORTED);
            assert(track.color_matrix == VIDEO_COLOR_UNSUPPORTED);
            assert(track.color_range == VIDEO_COLOR_RANGE_LIMITED);
            break;
        }
    }
    assert(video_track >= 0);
    assert(video_demux_decode_open(demux, video_track, error, sizeof(error)));
    VideoFrame frame;
    assert(video_demux_decode_next(demux, &frame, error, sizeof(error)) == -1);
    assert(strstr(error, "Unsupported SDR color metadata"));
    video_demux_close(demux);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char error[256];
    FileContext *files = file_create(argv[1]);
    assert(files);
    file_set_write_root(files, argv[1]);
    check_long_gop_seeks(files);
    check_unsupported_color(files);
    check_writer(files);

    assert(!video_demux_open_file(files, "assets/../clip.mp4", error, sizeof(error)));
    assert(error[0]);
    assert(!video_demux_open_file(files, "assets/missing.mp4", error, sizeof(error)));
    assert(error[0]);
#ifndef _WIN32
    assert(!video_demux_open_file(files, "assets/link.mp4", error, sizeof(error)));
    assert(error[0]);
#endif
    assert(!video_demux_open_memory(NULL, 0, error, sizeof(error)));
    assert(error[0]);
    const uint8_t invalid[] = "not a media file";
    assert(!video_demux_open_memory(invalid, sizeof(invalid), error, sizeof(error)));
    assert(error[0]);
    assert(!video_demux_open_memory(invalid, 64u * 1024u * 1024u + 1u,
                                    error, sizeof(error)));
    assert(error[0]);

    size_t size = 0;
    uint8_t *bytes = file_read_binary(files, "assets/clip.mp4", &size);
    assert(bytes && size);
    VideoDemux *decoder = video_demux_open_memory(bytes, size, error, sizeof(error));
    if (!decoder)
        fprintf(stderr, "%s\n", error);
    assert(decoder);
    check_decoded_frames(decoder);
    video_demux_close(decoder);
    VideoDemux *memory = video_demux_open_memory(bytes, size, error, sizeof(error));
    if (!memory)
        fprintf(stderr, "%s\n", error);
    assert(memory);
    int subtitle_tracks[2];
    int subtitle_track_count = 0;
    int audio_tracks[2];
    int audio_track_count = 0;
    for (int i = 0; i < video_demux_track_count(memory); ++i)
    {
        VideoTrack track;
        assert(video_demux_track(memory, i, &track));
        if (track.type == VIDEO_TRACK_AUDIO &&
            strcmp(track.codec, "aac") == 0)
        {
            assert(audio_track_count < 2);
            audio_tracks[audio_track_count++] = i;
        }
        if (track.type == VIDEO_TRACK_SUBTITLE &&
            strcmp(track.codec, "mov_text") == 0)
        {
            assert(subtitle_track_count < 2);
            subtitle_tracks[subtitle_track_count++] = i;
        }
    }
    assert(subtitle_track_count == 2);
    assert(audio_track_count == 2);
    check_audio_blocks(bytes, size, audio_tracks[0]);
    check_audio_blocks(bytes, size, audio_tracks[1]);
    check_subtitle_cues(bytes, size, subtitle_tracks[0], "Hello");
    check_subtitle_cues(bytes, size, subtitle_tracks[1], "Bonjour");
    memset(bytes, 0, size);
    free(bytes);
    VideoDemux *file = video_demux_open_file(files, "files/clip.mp4", error, sizeof(error));
    if (!file)
        fprintf(stderr, "%s\n", error);
    assert(file);
    file_destroy(files);
    check_tracks_and_packets(memory);
    check_tracks_and_packets(file);
    video_demux_close(memory);
    video_demux_close(file);
    video_demux_close(NULL);
    puts("video: MP4 tracks, packets, RGBA/PCM decode, mov_text cues, writer round trip and sandbox passed");
    return 0;
}