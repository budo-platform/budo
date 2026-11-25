#include "file/file_wrapper.h"
#include "video/video_reader.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct VideoDemux
{
    int next_ordinal;
    uint8_t pixels[16];
};

static int rewinds;
static int decodes;
static int invalid_ordinal = -1;

VideoDemux *video_demux_open_file(FileContext *files, const char *path,
                                  char *error, size_t error_size)
{
    (void)files;
    (void)path;
    (void)error;
    (void)error_size;
    VideoDemux *demux = calloc(1, sizeof(*demux));
    assert(demux);
    return demux;
}

void video_demux_close(VideoDemux *demux)
{
    free(demux);
}

int video_demux_track_count(const VideoDemux *demux)
{
    assert(demux);
    return 1;
}

int video_demux_track(const VideoDemux *demux, int index, VideoTrack *track)
{
    assert(demux && index == 0);
    *track = (VideoTrack){
        .type = VIDEO_TRACK_VIDEO,
        .codec = "h264",
        .metadata = VIDEO_TRACK_HAS_START_TIME | VIDEO_TRACK_HAS_DURATION |
                    VIDEO_TRACK_HAS_DIMENSIONS | VIDEO_TRACK_HAS_FRAME_RATE |
                    VIDEO_TRACK_HAS_SAMPLE_ASPECT_RATIO | VIDEO_TRACK_HAS_ROTATION,
        .time_base_num = 1, .time_base_den = 30,
        .duration = 120, .width = 2, .height = 2,
        .frame_rate_num = 30, .frame_rate_den = 1,
        .sample_aspect_ratio_num = 1, .sample_aspect_ratio_den = 1,
        .color_primaries = VIDEO_COLOR_BT709,
        .color_transfer = VIDEO_COLOR_BT709,
        .color_matrix = VIDEO_COLOR_BT709,
        .color_range = VIDEO_COLOR_RANGE_FULL,
    };
    return 1;
}

int video_demux_decode_open(VideoDemux *demux, int track,
                            char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    assert(demux && track == 0);
    return 1;
}

int video_demux_decode_rewind(VideoDemux *demux, char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    demux->next_ordinal = 0;
    rewinds++;
    return 1;
}

int video_demux_decode_next(VideoDemux *demux, VideoFrame *frame,
                            char *error, size_t error_size)
{
    (void)error;
    (void)error_size;
    if (demux->next_ordinal == 120)
        return 0;
    int ordinal = demux->next_ordinal++;
    decodes++;
    memset(demux->pixels, ordinal, sizeof(demux->pixels));
    *frame = (VideoFrame){
        .rgba = demux->pixels, .size = sizeof(demux->pixels),
        .width = 2, .height = 2, .stride = 8,
        .pts = ordinal, .duration = 1,
        .time_base_num = 1, .time_base_den = 30,
        .sample_aspect_ratio_num = 1, .sample_aspect_ratio_den = 1,
        .color_primaries = VIDEO_COLOR_BT709,
        .color_transfer = VIDEO_COLOR_BT709,
        .color_matrix = VIDEO_COLOR_BT709,
        .color_range = VIDEO_COLOR_RANGE_FULL,
        .interlaced = ordinal == invalid_ordinal,
    };
    return 1;
}

static VideoReaderEvent wait_for_event(VideoReader *reader)
{
    VideoReaderEvent event;
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        if (video_reader_poll(reader, &event))
            return event;
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!"Timed out waiting for video reader");
    return (VideoReaderEvent){0};
}

static VideoReaderEvent request(VideoReader *reader, uint64_t ordinal)
{
    char error[256];
    assert(video_reader_request_frame(reader, ordinal, error, sizeof(error)));
    return wait_for_event(reader);
}

static void check_frame(VideoReader *reader, uint64_t ordinal)
{
    VideoReaderEvent event = request(reader, ordinal);
    assert(event.type == VIDEO_READER_FRAME && event.ordinal == ordinal);
    assert(event.frame.pixels[0] == ordinal);
    assert(event.pts_us == (int64_t)((ordinal * 1000000 + 15) / 30));
    video_reader_event_free(&event);
}

int main(void)
{
    FileContext *files = file_create(".");
    assert(files);
    char error[256];
    VideoReader *reader = video_reader_open_file(
        files, "assets/clip.mp4", error, sizeof(error));
    assert(reader);
    VideoReaderEvent event = wait_for_event(reader);
    assert(event.type == VIDEO_READER_READY);
    video_reader_event_free(&event);

    check_frame(reader, 0);
    check_frame(reader, 3);
    check_frame(reader, 10);
    assert(rewinds == 1 && decodes == 11);

    check_frame(reader, 10);
    check_frame(reader, 2);
    assert(rewinds == 3 && decodes == 25);

    event = request(reader, 125);
    assert(event.type == VIDEO_READER_EOF);
    video_reader_event_free(&event);
    check_frame(reader, 3);
    assert(rewinds == 4);

    invalid_ordinal = 6;
    event = request(reader, 9);
    assert(event.type == VIDEO_READER_ERROR);
    assert(strcmp(event.code, "video.frame_unsupported") == 0);
    video_reader_event_free(&event);
    assert(rewinds == 4);
    invalid_ordinal = -1;
    check_frame(reader, 4);
    assert(rewinds == 5);

    video_reader_close(reader);
    file_destroy(files);
    return 0;
}