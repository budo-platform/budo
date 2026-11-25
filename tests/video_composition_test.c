#include "file/file_wrapper.h"
#include "video/video_composition.h"
#include "video/video_export.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void wait_a_little(void)
{
    Sleep(1);
}
#else
#include <time.h>
static void wait_a_little(void)
{
    const struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
#endif

static VideoExportEvent wait_for_export(VideoExport *export)
{
    VideoExportEvent event;
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        assert(video_export_poll(export, &event));
        if (event.status != VIDEO_EXPORT_RUNNING)
            return event;
        wait_a_little();
    }
    assert(0 && "Timed out waiting for video export");
    return (VideoExportEvent){0};
}

static void check_export(FileContext *files,
                         const VideoCompositionConfig *config)
{
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    char error[256];
    assert(!video_export_start_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        "assets/invalid-output.mp4", config, 1000000, error, sizeof(error)));
    assert(error[0] != '\0');

    VideoExport *async_export = video_export_start_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        "files/async-composition.mp4", config, 1000000, error, sizeof(error));
    if (!async_export)
        fprintf(stderr, "%s\n", error);
    assert(async_export && error[0] == '\0');
    VideoExportEvent event = wait_for_export(async_export);
    assert(event.status == VIDEO_EXPORT_SUCCEEDED);
    assert(event.completed_frames == 60);
    assert(event.message[0] == '\0');
    assert(file_is_file_at(files, FILE_ROOT_FILES, "async-composition.mp4"));
    video_export_close(async_export);

    async_export = video_export_start_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        "files/cancelled-composition.mp4", config, 1000000,
        error, sizeof(error));
    assert(async_export);
    assert(video_export_cancel(async_export));
    assert(!video_export_cancel(async_export));
    event = wait_for_export(async_export);
    assert(event.status == VIDEO_EXPORT_CANCELLED);
    assert(strcmp(event.code, "video.cancelled") == 0);
    assert(event.message[0] != '\0');
    assert(!file_is_file_at(files, FILE_ROOT_FILES,
                            "cancelled-composition.mp4"));
    video_export_close(async_export);

    assert(video_composition_export_file(
        files, "assets/background.mp4", "assets/inset.mp4",
        "files/composition.mp4", config, 1000000, error, sizeof(error)));
    assert(error[0] == '\0');
    assert(file_is_file_at(files, FILE_ROOT_FILES, "composition.mp4"));
    assert(!video_composition_export_file(
        files, "assets/not-30-fps.mp4", "assets/inset.mp4",
        "files/rejected.mp4", config, 1000000, error, sizeof(error)));
    assert(error[0]);
    assert(!file_is_file_at(files, FILE_ROOT_FILES, "rejected.mp4"));

    VideoDemux *demux = video_demux_open_file(
        files, "files/composition.mp4", error, sizeof(error));
    if (!demux)
        fprintf(stderr, "%s\n", error);
    assert(demux);
    int video_track = -1;
    VideoTrack track;
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        assert(video_demux_track(demux, i, &track));
        if (track.type == VIDEO_TRACK_VIDEO)
        {
            video_track = i;
            break;
        }
    }
    assert(video_track >= 0);
    assert(track.width == 640 && track.height == 360);
    assert((track.metadata & VIDEO_TRACK_HAS_DURATION) != 0);
    double duration_seconds = track.duration * (double)track.time_base_num /
                              track.time_base_den;
    assert(fabs(duration_seconds - 2.0) <= 1.0 / 90000.0);
    assert(video_demux_decode_open(demux, video_track, error, sizeof(error)));

    VideoFrame frame;
    int frame_count = 0;
    int64_t previous_pts = INT64_MIN;
    double last_frame_time = 0.0;
    int status;
    while ((status = video_demux_decode_next(demux, &frame,
                                             error, sizeof(error))) == 1)
    {
        assert(frame.pts > previous_pts);
        double frame_time = frame.pts * (double)frame.time_base_num /
                            frame.time_base_den;
        if (fabs(frame_time - (double)frame_count / 30.0) >= 0.001)
            fprintf(stderr, "frame %d pts %lld tb %d/%d time %.6f\n",
                    frame_count, (long long)frame.pts, frame.time_base_num,
                    frame.time_base_den, frame_time);
        assert(fabs(frame_time - (double)frame_count / 30.0) < 0.001);
        last_frame_time = frame_time;
        const uint8_t *background = frame.rgba + ((size_t)20 * 640 + 20) * 4;
        const uint8_t *inset = frame.rgba + ((size_t)300 * 640 + 500) * 4;
        assert(background[0] > 210 && background[1] < 50 &&
               background[2] < 50);
        assert(inset[0] < 50 && inset[1] < 50 && inset[2] > 210);
        previous_pts = frame.pts;
        frame_count++;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && frame_count == 60);
    assert(fabs((last_frame_time + 1.0 / 30.0) - duration_seconds) <=
           1.0 / 90000.0);
    video_demux_close(demux);
#else
    (void)files;
    (void)config;
#endif
}

static void check_composition_seek(VideoComposition *composition,
                                   uint8_t *output, size_t output_size,
                                   int64_t ordinal, char *error,
                                   size_t error_size)
{
    assert(video_composition_seek_frame(composition, ordinal,
                                        error, error_size) == 1);
    int64_t pts_us = -1;
    assert(video_composition_next(composition, output, output_size, &pts_us,
                                  error, error_size) == 1);
    assert(pts_us == (ordinal * 1000000 + 15) / 30);
    const uint8_t *background = output + ((size_t)20 * 640 + 20) * 4;
    const uint8_t *inset = output + ((size_t)300 * 640 + 500) * 4;
    assert(background[0] > 220 && background[1] < 35 &&
           background[2] < 35 && background[3] == 255);
    assert(inset[0] < 35 && inset[1] < 35 &&
           inset[2] > 220 && inset[3] == 255);
    if (ordinal + 1 < video_composition_frame_count(composition))
    {
        assert(video_composition_next(composition, output, output_size,
                                      &pts_us, error, error_size) == 1);
        assert(pts_us == ((ordinal + 1) * 1000000 + 15) / 30);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FileContext *files = file_create(argv[1]);
    assert(files);
    file_set_write_root(files, argv[1]);

    VideoCompositionConfig config = {640, 360, 464, 254, 160, 90};
    char error[256];
    VideoComposition *composition = video_composition_open_files(
        files, "assets/background.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    if (!composition)
        fprintf(stderr, "%s\n", error);
    assert(composition);
    VideoComposition *unsupported = video_composition_open_files(
        files, "assets/not-30-fps.mp4", "assets/inset.mp4",
        &config, error, sizeof(error));
    assert(!unsupported);
    assert(strstr(error, "30 fps"));

    const size_t output_size = 640u * 360u * 4u;
    uint8_t *output = malloc(output_size);
    assert(output);
    int64_t pts_us;
    int frame_count = 0;
    int status;
    while ((status = video_composition_next(composition, output, output_size,
                                            &pts_us, error, sizeof(error))) == 1)
    {
        assert(pts_us == ((int64_t)frame_count * 1000000 + 15) / 30);
        const uint8_t *background = output + ((size_t)20 * 640 + 20) * 4;
        const uint8_t *inset = output + ((size_t)300 * 640 + 500) * 4;
        assert(background[0] > 220 && background[1] < 35 &&
               background[2] < 35 && background[3] == 255);
        assert(inset[0] < 35 && inset[1] < 35 &&
               inset[2] > 220 && inset[3] == 255);
        frame_count++;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0 && error[0] == '\0');
    assert(frame_count == 60);
    assert(video_composition_next(composition, output, output_size,
                                  &pts_us, error, sizeof(error)) == 0);

    check_composition_seek(composition, output, output_size, 47,
                           error, sizeof(error));
    check_composition_seek(composition, output, output_size, 4,
                           error, sizeof(error));
    check_composition_seek(composition, output, output_size, 59,
                           error, sizeof(error));
    assert(video_composition_seek_frame(composition, 60,
                                        error, sizeof(error)) == 0);
    assert(error[0] == '\0');
    assert(video_composition_seek_frame(composition, 1,
                                        error, sizeof(error)) == 1);
    assert(video_composition_next(composition, output, output_size, &pts_us,
                                  error, sizeof(error)) == 1);
    assert(pts_us == 33333);
    assert(video_composition_seek_frame(composition, -1,
                                        error, sizeof(error)) == -1);
    assert(error[0] != '\0');

    free(output);
    video_composition_close(composition);
    check_export(files, &config);
    video_composition_close(NULL);
    file_destroy(files);
    return 0;
}