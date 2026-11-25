#ifndef BUDO_VIDEO_DEMUX_H
#define BUDO_VIDEO_DEMUX_H

#include <stddef.h>
#include <stdint.h>

typedef struct FileContext FileContext;
typedef struct VideoDemux VideoDemux;
typedef struct VideoWriter VideoWriter;

typedef enum VideoTrackMetadata
{
    VIDEO_TRACK_HAS_LANGUAGE = 1u << 0,
    VIDEO_TRACK_HAS_TITLE = 1u << 1,
    VIDEO_TRACK_HAS_START_TIME = 1u << 2,
    VIDEO_TRACK_HAS_DURATION = 1u << 3,
    VIDEO_TRACK_HAS_DIMENSIONS = 1u << 4,
    VIDEO_TRACK_HAS_FRAME_RATE = 1u << 5,
    VIDEO_TRACK_HAS_SAMPLE_ASPECT_RATIO = 1u << 6,
    VIDEO_TRACK_HAS_ROTATION = 1u << 7
} VideoTrackMetadata;

typedef enum VideoColorValue
{
    VIDEO_COLOR_UNKNOWN,
    VIDEO_COLOR_BT709,
    VIDEO_COLOR_UNSUPPORTED
} VideoColorValue;

typedef enum VideoColorRange
{
    VIDEO_COLOR_RANGE_UNKNOWN,
    VIDEO_COLOR_RANGE_LIMITED,
    VIDEO_COLOR_RANGE_FULL,
    VIDEO_COLOR_RANGE_UNSUPPORTED
} VideoColorRange;

typedef enum VideoTrackType
{
    VIDEO_TRACK_UNKNOWN,
    VIDEO_TRACK_VIDEO,
    VIDEO_TRACK_AUDIO,
    VIDEO_TRACK_SUBTITLE,
    VIDEO_TRACK_DATA,
    VIDEO_TRACK_ATTACHMENT
} VideoTrackType;

typedef struct VideoTrack
{
    VideoTrackType type;
    char codec[64];
    char language[64];
    char title[128];
    int time_base_num;
    int time_base_den;
    uint32_t metadata;
    int64_t start_time;
    int64_t duration;
    int width;
    int height;
    int frame_rate_num;
    int frame_rate_den;
    int sample_aspect_ratio_num;
    int sample_aspect_ratio_den;
    int rotation_degrees; 
    VideoColorValue color_primaries;
    VideoColorValue color_transfer;
    VideoColorValue color_matrix;
    VideoColorRange color_range;
    int sample_rate;
    int channels;
    char channel_layout[64];
    int disposition_default;
    int disposition_forced;
} VideoTrack;

typedef struct VideoPacket
{
    int track;
    const uint8_t *data;
    size_t size;
    int64_t pts;
    int64_t dts;
    int64_t duration;
    int keyframe;
} VideoPacket;

typedef struct VideoFrame
{
    const uint8_t *rgba;
    size_t size;
    int width;
    int height;
    int stride;
    int64_t pts;
    int64_t duration;
    int time_base_num;
    int time_base_den;
    int sample_aspect_ratio_num;
    int sample_aspect_ratio_den;
    VideoColorValue color_primaries;
    VideoColorValue color_transfer;
    VideoColorValue color_matrix;
    VideoColorRange color_range;
    int interlaced;
} VideoFrame;

typedef struct VideoAudioBlock
{
    const float *samples;
    size_t frame_count;
    int sample_rate;
    int channels;
    int64_t pts_us;
    int64_t duration_us;
} VideoAudioBlock;

typedef struct VideoSubtitle
{
    const char *text;
    size_t text_size;
    int64_t start_time_us;
    int64_t end_time_us;
} VideoSubtitle;

VideoDemux *video_demux_open_file(FileContext *files, const char *path,
                                  char *error, size_t error_size);
VideoDemux *video_demux_open_memory(const uint8_t *data, size_t size,
                                    char *error, size_t error_size);
void video_demux_close(VideoDemux *demux);
int video_demux_track_count(const VideoDemux *demux);

int video_demux_track(const VideoDemux *demux, int index, VideoTrack *track);

int video_demux_next(VideoDemux *demux, VideoPacket *packet,
                     char *error, size_t error_size);

int video_demux_decode_open(VideoDemux *demux, int track,
                            char *error, size_t error_size);
int video_demux_decode_next(VideoDemux *demux, VideoFrame *frame,
                            char *error, size_t error_size);

int video_demux_seek_time(VideoDemux *demux, int64_t timestamp,
                          VideoFrame *frame, char *error, size_t error_size);

int video_demux_decode_rewind(VideoDemux *demux, char *error,
                              size_t error_size);

int video_demux_seek_frame(VideoDemux *demux, uint64_t ordinal,
                           VideoFrame *frame, char *error, size_t error_size);

int video_demux_audio_open(VideoDemux *demux, int track,
                           char *error, size_t error_size);

int video_demux_audio_next(VideoDemux *demux, VideoAudioBlock *block,
                           char *error, size_t error_size);

int video_demux_subtitle_open(VideoDemux *demux, int track,
                              char *error, size_t error_size);

int video_demux_subtitle_next(VideoDemux *demux, VideoSubtitle *subtitle,
                              char *error, size_t error_size);

typedef struct VideoWriterSubtitleConfig
{
    const char *language;
    const char *title;
    int disposition_default;
    int disposition_forced;
} VideoWriterSubtitleConfig;

typedef struct VideoWriterConfig
{
    int width;
    int height;
    int frame_rate_num;
    int frame_rate_den;
    int video_bit_rate;
    int audio_sample_rate;
    int audio_channels;
    int audio_bit_rate;
    const VideoWriterSubtitleConfig *subtitles;
    size_t subtitle_count;
} VideoWriterConfig;

VideoWriter *video_writer_open_file(FileContext *files, const char *path,
                                    const VideoWriterConfig *config,
                                    char *error, size_t error_size);

int video_writer_write_frame(VideoWriter *writer, const uint8_t *rgba,
                             size_t size, int64_t pts_us,
                             char *error, size_t error_size);

int video_writer_write_audio(VideoWriter *writer, const float *samples,
                             size_t frame_count, int64_t pts_us,
                             char *error, size_t error_size);

int video_writer_write_subtitle(VideoWriter *writer, size_t track,
                                const char *text, size_t text_size,
                                int64_t start_time_us, int64_t end_time_us,
                                char *error, size_t error_size);

int video_writer_finish(VideoWriter *writer, char *error, size_t error_size);
void video_writer_close(VideoWriter *writer);

#endif