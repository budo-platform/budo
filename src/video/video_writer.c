#include "video/video_demux.h"
#include "file/file_wrapper.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define VIDEO_WRITER_IO_BUFFER_SIZE (32 * 1024)
#define VIDEO_WRITER_MAX_PIXELS (16u * 1024u * 1024u)
#define VIDEO_WRITER_MAX_AUDIO_VALUES (1024u * 1024u)
#define VIDEO_WRITER_MAX_SUBTITLE_TEXT_SIZE (1024u * 1024u)

typedef struct VideoWriterSubtitleTrack
{
    AVCodecContext *encoder;
    AVStream *stream;
    int64_t last_pts;
    int have_pts;
} VideoWriterSubtitleTrack;

struct VideoWriter
{
    FileContext *files;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    FileAtomicWriter *file;
#endif
    AVFormatContext *format;
    AVIOContext *io;
    AVPacket *packet;
    AVCodecContext *video_encoder;
    AVCodecContext *audio_encoder;
    AVFrame *video_frame;
    AVFrame *audio_frame;
    AVAudioFifo *audio_fifo;
    VideoWriterSubtitleTrack *subtitles;
    size_t subtitle_count;
    struct SwsContext *scaler;
    SwrContext *resampler;
    AVStream *video_stream;
    AVStream *audio_stream;
    int64_t last_video_pts;
    int64_t expected_audio_pts;
    int64_t next_audio_pts;
    int have_video_pts;
    int have_audio_pts;
    int io_failed;
    int failed;
    int header_written;
};

static void set_error(char *error, size_t size, const char *message)
{
    if (error && size)
        snprintf(error, size, "%s", message);
}

static void set_av_error(char *error, size_t size, const char *operation, int code)
{
    char detail[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, detail, sizeof(detail));
    if (error && size)
        snprintf(error, size, "%s: %s", operation, detail);
}

static int valid_utf8(const uint8_t *text, size_t size)
{
    for (size_t i = 0; i < size;)
    {
        uint8_t first = text[i++];
        if (first < 0x80)
            continue;
        size_t continuation_count;
        uint8_t second_min = 0x80;
        uint8_t second_max = 0xbf;
        if (first >= 0xc2 && first <= 0xdf)
            continuation_count = 1;
        else if (first == 0xe0)
        {
            continuation_count = 2;
            second_min = 0xa0;
        }
        else if ((first >= 0xe1 && first <= 0xec) ||
                 (first >= 0xee && first <= 0xef))
            continuation_count = 2;
        else if (first == 0xed)
        {
            continuation_count = 2;
            second_max = 0x9f;
        }
        else if (first == 0xf0)
        {
            continuation_count = 3;
            second_min = 0x90;
        }
        else if (first >= 0xf1 && first <= 0xf3)
            continuation_count = 3;
        else if (first == 0xf4)
        {
            continuation_count = 3;
            second_max = 0x8f;
        }
        else
            return 0;
        if (continuation_count > size - i ||
            text[i] < second_min || text[i] > second_max)
            return 0;
        i++;
        for (size_t j = 1; j < continuation_count; ++j, ++i)
            if (text[i] < 0x80 || text[i] > 0xbf)
                return 0;
    }
    return 1;
}

static int metadata_value_fits(const char *value, size_t max_size)
{
    if (!value)
        return 1;
    for (size_t i = 0; i <= max_size; ++i)
        if (!value[i])
            return 1;
    return 0;
}

#if LIBAVFORMAT_VERSION_MAJOR >= 61
static int write_sink(void *opaque, const uint8_t *buffer, int size)
#else
static int write_sink(void *opaque, uint8_t *buffer, int size)
#endif
{
    VideoWriter *writer = opaque;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (size < 0 || !file_atomic_writer_write(writer->file, buffer, (size_t)size))
    {
        writer->io_failed = 1;
        return AVERROR(EIO);
    }
    return size;
#else
    (void)buffer;
    (void)size;
    writer->io_failed = 1;
    return AVERROR(ENOSYS);
#endif
}

static int release_writer(VideoWriter *writer, int commit)
{
    if (!writer)
        return 0;
    int sink_ok = 1;
    sws_freeContext(writer->scaler);
    swr_free(&writer->resampler);
    av_audio_fifo_free(writer->audio_fifo);
    av_frame_free(&writer->video_frame);
    av_frame_free(&writer->audio_frame);
    avcodec_free_context(&writer->video_encoder);
    avcodec_free_context(&writer->audio_encoder);
    for (size_t i = 0; i < writer->subtitle_count; ++i)
        avcodec_free_context(&writer->subtitles[i].encoder);
    free(writer->subtitles);
    av_packet_free(&writer->packet);
    if (writer->format)
    {
        writer->format->pb = NULL;
        avformat_free_context(writer->format);
    }
    if (writer->io)
    {
        av_freep(&writer->io->buffer);
        avio_context_free(&writer->io);
    }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (writer->file)
        sink_ok = file_atomic_writer_finish(writer->file, commit);
#else
    (void)commit;
#endif
    free(writer);
    return sink_ok;
}

static int write_packets(VideoWriter *writer, AVCodecContext *encoder,
                         AVStream *stream, char *error, size_t error_size)
{
    for (;;)
    {
        int result = avcodec_receive_packet(encoder, writer->packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return 1;
        if (result < 0)
        {
            set_av_error(error, error_size, "Cannot receive encoded packet", result);
            return 0;
        }
        av_packet_rescale_ts(writer->packet, encoder->time_base, stream->time_base);
        writer->packet->stream_index = stream->index;
        result = av_interleaved_write_frame(writer->format, writer->packet);
        av_packet_unref(writer->packet);
        if (result < 0 || writer->io_failed)
        {
            if (writer->io_failed)
                set_error(error, error_size, "Cannot write staged media file");
            else
                set_av_error(error, error_size, "Cannot mux encoded packet", result);
            return 0;
        }
    }
}

static int encode_frame(VideoWriter *writer, AVCodecContext *encoder,
                        AVStream *stream, AVFrame *frame,
                        char *error, size_t error_size)
{
    int result = avcodec_send_frame(encoder, frame);
    if (result == AVERROR(EAGAIN))
    {
        if (!write_packets(writer, encoder, stream, error, error_size))
            return 0;
        result = avcodec_send_frame(encoder, frame);
    }
    if (result < 0 && result != AVERROR_EOF)
    {
        set_av_error(error, error_size, "Cannot submit encoder frame", result);
        return 0;
    }
    return write_packets(writer, encoder, stream, error, error_size);
}

static int open_video_encoder(VideoWriter *writer, const VideoWriterConfig *config,
                              char *error, size_t error_size)
{
    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
    {
        set_error(error, error_size, "The libx264 video encoder is unavailable");
        return 0;
    }
    writer->video_stream = avformat_new_stream(writer->format, NULL);
    writer->video_encoder = avcodec_alloc_context3(codec);
    if (!writer->video_stream || !writer->video_encoder)
    {
        set_error(error, error_size, "Out of memory allocating video encoder");
        return 0;
    }
    AVCodecContext *encoder = writer->video_encoder;
    encoder->codec_id = AV_CODEC_ID_H264;
    encoder->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder->width = config->width;
    encoder->height = config->height;
    encoder->sample_aspect_ratio = (AVRational){1, 1};
    encoder->pix_fmt = (config->width & 1) || (config->height & 1)
                           ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P;
    encoder->time_base = (AVRational){1, 90000};
    encoder->framerate = (AVRational){config->frame_rate_num,
                                      config->frame_rate_den};
    encoder->bit_rate = config->video_bit_rate;
    encoder->gop_size = config->frame_rate_num / config->frame_rate_den;
    if (encoder->gop_size < 1)
        encoder->gop_size = 1;
    
    encoder->max_b_frames = 0;
    encoder->thread_count = 1;
    encoder->color_range = AVCOL_RANGE_MPEG;
    encoder->colorspace = AVCOL_SPC_BT709;
    encoder->color_primaries = AVCOL_PRI_BT709;
    encoder->color_trc = AVCOL_TRC_BT709;
    if (writer->format->oformat->flags & AVFMT_GLOBALHEADER)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *options = NULL;
    av_dict_set(&options, "preset", "veryfast", 0);
    int result = avcodec_open2(encoder, codec, &options);
    av_dict_free(&options);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot open libx264 encoder", result);
        return 0;
    }
    result = avcodec_parameters_from_context(writer->video_stream->codecpar,
                                              encoder);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot configure MP4 video track", result);
        return 0;
    }
    writer->video_stream->time_base = encoder->time_base;

    writer->video_frame = av_frame_alloc();
    if (!writer->video_frame)
    {
        set_error(error, error_size, "Out of memory allocating video frame");
        return 0;
    }
    writer->video_frame->format = encoder->pix_fmt;
    writer->video_frame->width = encoder->width;
    writer->video_frame->height = encoder->height;
    writer->video_frame->color_range = encoder->color_range;
    writer->video_frame->colorspace = encoder->colorspace;
    writer->video_frame->color_primaries = encoder->color_primaries;
    writer->video_frame->color_trc = encoder->color_trc;
    result = av_frame_get_buffer(writer->video_frame, 32);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot allocate video frame pixels", result);
        return 0;
    }
    return 1;
}

static int open_audio_encoder(VideoWriter *writer, const VideoWriterConfig *config,
                              char *error, size_t error_size)
{
    if (config->audio_sample_rate == 0 && config->audio_channels == 0 &&
        config->audio_bit_rate == 0)
        return 1;
    if ((config->audio_sample_rate != 44100 && config->audio_sample_rate != 48000) ||
        (config->audio_channels != 1 && config->audio_channels != 2) ||
        config->audio_bit_rate <= 0)
    {
        set_error(error, error_size, "AAC output requires 44.1/48 kHz mono/stereo and a positive bitrate");
        return 0;
    }
    const AVCodec *codec = avcodec_find_encoder_by_name("aac");
    if (!codec)
    {
        set_error(error, error_size, "The AAC audio encoder is unavailable");
        return 0;
    }
    writer->audio_stream = avformat_new_stream(writer->format, NULL);
    writer->audio_encoder = avcodec_alloc_context3(codec);
    if (!writer->audio_stream || !writer->audio_encoder)
    {
        set_error(error, error_size, "Out of memory allocating audio encoder");
        return 0;
    }
    AVCodecContext *encoder = writer->audio_encoder;
    encoder->codec_id = AV_CODEC_ID_AAC;
    encoder->codec_type = AVMEDIA_TYPE_AUDIO;
    encoder->sample_rate = config->audio_sample_rate;
    encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
    encoder->time_base = (AVRational){1, encoder->sample_rate};
    encoder->bit_rate = config->audio_bit_rate;
    encoder->thread_count = 1;
    av_channel_layout_default(&encoder->ch_layout, config->audio_channels);
    if (writer->format->oformat->flags & AVFMT_GLOBALHEADER)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int result = avcodec_open2(encoder, codec, NULL);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot open AAC encoder", result);
        return 0;
    }
    result = avcodec_parameters_from_context(writer->audio_stream->codecpar,
                                              encoder);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot configure MP4 audio track", result);
        return 0;
    }
    writer->audio_stream->time_base = encoder->time_base;
    result = swr_alloc_set_opts2(&writer->resampler,
                                 &encoder->ch_layout, encoder->sample_fmt,
                                 encoder->sample_rate, &encoder->ch_layout,
                                 AV_SAMPLE_FMT_FLT, encoder->sample_rate,
                                 0, NULL);
    if (result >= 0)
        result = swr_init(writer->resampler);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot configure PCM conversion", result);
        return 0;
    }
    writer->audio_fifo = av_audio_fifo_alloc(encoder->sample_fmt,
                                              encoder->ch_layout.nb_channels,
                                              encoder->frame_size * 2);
    writer->audio_frame = av_frame_alloc();
    if (!writer->audio_fifo || !writer->audio_frame)
    {
        set_error(error, error_size, "Out of memory allocating audio buffers");
        return 0;
    }
    writer->audio_frame->format = encoder->sample_fmt;
    writer->audio_frame->sample_rate = encoder->sample_rate;
    writer->audio_frame->nb_samples = encoder->frame_size;
    result = av_channel_layout_copy(&writer->audio_frame->ch_layout,
                                    &encoder->ch_layout);
    if (result >= 0)
        result = av_frame_get_buffer(writer->audio_frame, 0);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot allocate AAC frame samples", result);
        return 0;
    }
    return 1;
}

static int open_subtitle_encoders(VideoWriter *writer,
                                  const VideoWriterConfig *config,
                                  char *error, size_t error_size)
{
    static const char subtitle_header[] =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Serif,18,&H00FFFFFF,&H000000FF,&H00000000,"
        "&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
        "Effect, Text\n";
    if (!config->subtitle_count)
        return 1;
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MOV_TEXT);
    if (!codec)
    {
        set_error(error, error_size, "The mov_text subtitle encoder is unavailable");
        return 0;
    }
    writer->subtitles = calloc(config->subtitle_count, sizeof(*writer->subtitles));
    if (!writer->subtitles)
    {
        set_error(error, error_size, "Out of memory allocating subtitle tracks");
        return 0;
    }
    writer->subtitle_count = config->subtitle_count;
    for (size_t i = 0; i < writer->subtitle_count; ++i)
    {
        const VideoWriterSubtitleConfig *subtitle = &config->subtitles[i];
        if (!metadata_value_fits(subtitle->language, 63) ||
            !metadata_value_fits(subtitle->title, 127))
        {
            set_error(error, error_size, "Subtitle metadata value is too long");
            return 0;
        }
        VideoWriterSubtitleTrack *track = &writer->subtitles[i];
        track->stream = avformat_new_stream(writer->format, NULL);
        track->encoder = avcodec_alloc_context3(codec);
        if (!track->stream || !track->encoder)
        {
            set_error(error, error_size, "Out of memory allocating subtitle encoder");
            return 0;
        }
        track->encoder->codec_id = AV_CODEC_ID_MOV_TEXT;
        track->encoder->codec_type = AVMEDIA_TYPE_SUBTITLE;
        track->encoder->time_base = (AVRational){1, 1000};
        track->encoder->width = writer->video_encoder->width;
        track->encoder->height = writer->video_encoder->height;
        track->encoder->subtitle_header =
            av_malloc(sizeof(subtitle_header));
        if (!track->encoder->subtitle_header)
        {
            set_error(error, error_size, "Out of memory configuring subtitle encoder");
            return 0;
        }
        memcpy(track->encoder->subtitle_header, subtitle_header,
               sizeof(subtitle_header));
        track->encoder->subtitle_header_size = sizeof(subtitle_header);
        if (writer->format->oformat->flags & AVFMT_GLOBALHEADER)
            track->encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        int result = avcodec_open2(track->encoder, codec, NULL);
        if (result < 0)
        {
            set_av_error(error, error_size, "Cannot open mov_text encoder", result);
            return 0;
        }
        result = avcodec_parameters_from_context(track->stream->codecpar,
                                                  track->encoder);
        if (result < 0)
        {
            set_av_error(error, error_size, "Cannot configure MP4 subtitle track", result);
            return 0;
        }
        track->stream->time_base = track->encoder->time_base;
        if (subtitle->language &&
            av_dict_set(&track->stream->metadata, "language",
                        subtitle->language, 0) < 0)
        {
            set_error(error, error_size, "Cannot set subtitle language metadata");
            return 0;
        }
        if (subtitle->title &&
            av_dict_set(&track->stream->metadata, "title",
                        subtitle->title, 0) < 0)
        {
            set_error(error, error_size, "Cannot set subtitle title metadata");
            return 0;
        }
        if (subtitle->disposition_default)
            track->stream->disposition |= AV_DISPOSITION_DEFAULT;
        if (subtitle->disposition_forced)
            track->stream->disposition |= AV_DISPOSITION_FORCED;
    }
    return 1;
}

VideoWriter *video_writer_open_file(FileContext *files, const char *path,
                                    const VideoWriterConfig *config,
                                    char *error, size_t error_size)
{
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)files;
    (void)path;
    (void)config;
    set_error(error, error_size, "Atomic media output is unavailable on this platform");
    return NULL;
#else
    if (!files || !path || !config ||
        !file_virtual_path_is_valid(path, 0, 1) ||
        config->width <= 0 || config->height <= 0 ||
        (uint64_t)config->width * (uint64_t)config->height > VIDEO_WRITER_MAX_PIXELS ||
        config->frame_rate_num <= 0 || config->frame_rate_den <= 0 ||
        config->frame_rate_num > 240000 || config->frame_rate_den > 10000 ||
        config->frame_rate_num > 240 * config->frame_rate_den ||
        config->video_bit_rate <= 0 ||
        (config->subtitle_count && !config->subtitles) ||
        config->subtitle_count >
            63 - ((config->audio_sample_rate || config->audio_channels ||
                   config->audio_bit_rate)
                      ? 1u
                      : 0u))
    {
        set_error(error, error_size, "Invalid MP4 writer configuration or output path");
        return NULL;
    }
    VideoWriter *writer = calloc(1, sizeof(*writer));
    if (!writer)
    {
        set_error(error, error_size, "Out of memory allocating media writer");
        return NULL;
    }
    writer->last_video_pts = INT64_MIN;
    writer->expected_audio_pts = INT64_MIN;
    writer->next_audio_pts = INT64_MIN;
    writer->files = files;
    writer->file = file_atomic_writer_open(files, path);
    if (!writer->file)
    {
        set_error(error, error_size, file_get_error(files));
        release_writer(writer, 0);
        return NULL;
    }
    int result = avformat_alloc_output_context2(&writer->format, NULL, "mp4", NULL);
    if (result < 0 || !writer->format)
    {
        if (result < 0)
            set_av_error(error, error_size, "Cannot create MP4 muxer", result);
        else
            set_error(error, error_size, "Out of memory allocating MP4 muxer");
        release_writer(writer, 0);
        return NULL;
    }
    uint8_t *buffer = av_malloc(VIDEO_WRITER_IO_BUFFER_SIZE);
    if (!buffer)
    {
        set_error(error, error_size, "Out of memory allocating media output buffer");
        release_writer(writer, 0);
        return NULL;
    }
    writer->io = avio_alloc_context(buffer, VIDEO_WRITER_IO_BUFFER_SIZE, 1,
                                    writer, NULL, write_sink, NULL);
    if (!writer->io)
    {
        av_free(buffer);
        set_error(error, error_size, "Out of memory allocating media output");
        release_writer(writer, 0);
        return NULL;
    }
    writer->format->pb = writer->io;
    writer->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    writer->format->max_interleave_delta = AV_TIME_BASE;
    if (!open_video_encoder(writer, config, error, error_size) ||
        !open_audio_encoder(writer, config, error, error_size) ||
        !open_subtitle_encoders(writer, config, error, error_size))
    {
        release_writer(writer, 0);
        return NULL;
    }
    writer->packet = av_packet_alloc();
    if (!writer->packet)
    {
        set_error(error, error_size, "Out of memory allocating encoded packet");
        release_writer(writer, 0);
        return NULL;
    }
    AVDictionary *options = NULL;
    av_dict_set(&options, "movflags",
                "empty_moov+delay_moov+frag_keyframe+default_base_moof+negative_cts_offsets",
                0);
    result = avformat_write_header(writer->format, &options);
    av_dict_free(&options);
    if (result < 0 || writer->io_failed)
    {
        if (writer->io_failed)
            set_error(error, error_size, "Cannot write staged media file");
        else
            set_av_error(error, error_size, "Cannot write MP4 header", result);
        release_writer(writer, 0);
        return NULL;
    }
    writer->header_written = 1;
    set_error(error, error_size, "");
    return writer;
#endif
}

int video_writer_write_frame(VideoWriter *writer, const uint8_t *rgba,
                             size_t size, int64_t pts_us,
                             char *error, size_t error_size)
{
    if (!writer || !writer->video_encoder || !rgba || writer->failed ||
        pts_us < 0 || (uint64_t)writer->video_encoder->width *
                          (uint64_t)writer->video_encoder->height >
                          SIZE_MAX / 4 ||
        size != (size_t)writer->video_encoder->width *
                    (size_t)writer->video_encoder->height * 4)
    {
        set_error(error, error_size, "Invalid video frame");
        return 0;
    }
    int64_t pts = av_rescale_q(pts_us, AV_TIME_BASE_Q,
                               writer->video_encoder->time_base);
    if (writer->have_video_pts && pts <= writer->last_video_pts)
    {
        set_error(error, error_size, "Video frame timestamps must strictly increase");
        return 0;
    }
    int result = av_frame_make_writable(writer->video_frame);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot prepare video frame", result);
        writer->failed = 1;
        return 0;
    }
    writer->scaler = sws_getCachedContext(writer->scaler,
                                          writer->video_encoder->width,
                                          writer->video_encoder->height,
                                          AV_PIX_FMT_RGBA,
                                          writer->video_encoder->width,
                                          writer->video_encoder->height,
                                          writer->video_encoder->pix_fmt,
                                          SWS_BILINEAR, NULL, NULL, NULL);
    if (!writer->scaler)
    {
        set_error(error, error_size, "Cannot configure video pixel conversion");
        writer->failed = 1;
        return 0;
    }
    const int *coefficients = sws_getCoefficients(SWS_CS_ITU709);
    result = sws_setColorspaceDetails(writer->scaler, coefficients, 1,
                                      coefficients, 0, 0, 1 << 16, 1 << 16);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot configure video color conversion", result);
        writer->failed = 1;
        return 0;
    }
    const uint8_t *source[] = {rgba, NULL, NULL, NULL};
    int source_stride[] = {writer->video_encoder->width * 4, 0, 0, 0};
    result = sws_scale(writer->scaler, source, source_stride, 0,
                       writer->video_encoder->height,
                       writer->video_frame->data, writer->video_frame->linesize);
    if (result != writer->video_encoder->height)
    {
        set_error(error, error_size, "Cannot convert video frame");
        writer->failed = 1;
        return 0;
    }
    writer->video_frame->pts = pts;
    int64_t frame_duration = av_rescale_q(
        1, (AVRational){writer->video_encoder->framerate.den,
                        writer->video_encoder->framerate.num},
        writer->video_encoder->time_base);
    if (frame_duration <= 0)
    {
        set_error(error, error_size, "Cannot represent video frame duration");
        writer->failed = 1;
        return 0;
    }
#if LIBAVUTIL_VERSION_MAJOR >= 58
    writer->video_frame->duration = frame_duration;
#else
    writer->video_frame->pkt_duration = frame_duration;
#endif
    if (!encode_frame(writer, writer->video_encoder, writer->video_stream,
                      writer->video_frame, error, error_size))
    {
        writer->failed = 1;
        return 0;
    }
    writer->last_video_pts = pts;
    writer->have_video_pts = 1;
    set_error(error, error_size, "");
    return 1;
}

static int encode_audio_fifo(VideoWriter *writer, int flush,
                             char *error, size_t error_size)
{
    AVCodecContext *encoder = writer->audio_encoder;
    int available = av_audio_fifo_size(writer->audio_fifo);
    while (available >= encoder->frame_size ||
           (flush && available > 0))
    {
        int samples = available >= encoder->frame_size
                          ? encoder->frame_size : available;
        int result = av_frame_make_writable(writer->audio_frame);
        if (result < 0)
        {
            set_av_error(error, error_size, "Cannot prepare AAC frame", result);
            return 0;
        }
        writer->audio_frame->nb_samples = encoder->frame_size;
        result = av_samples_set_silence(writer->audio_frame->extended_data,
                                        0, encoder->frame_size,
                                        encoder->ch_layout.nb_channels,
                                        encoder->sample_fmt);
        if (result < 0)
        {
            set_av_error(error, error_size, "Cannot pad AAC frame", result);
            return 0;
        }
        int read = av_audio_fifo_read(writer->audio_fifo,
                                      (void **)writer->audio_frame->extended_data,
                                      samples);
        if (read != samples)
        {
            set_error(error, error_size, "Cannot read PCM audio queue");
            return 0;
        }
        writer->audio_frame->pts = writer->next_audio_pts;
        writer->next_audio_pts += encoder->frame_size;
        if (!encode_frame(writer, encoder, writer->audio_stream,
                          writer->audio_frame, error, error_size))
            return 0;
        available = av_audio_fifo_size(writer->audio_fifo);
    }
    return 1;
}

int video_writer_write_audio(VideoWriter *writer, const float *samples,
                             size_t frame_count, int64_t pts_us,
                             char *error, size_t error_size)
{
    if (!writer || !writer->audio_encoder || writer->failed || !samples ||
        !frame_count || pts_us < 0 ||
        frame_count > VIDEO_WRITER_MAX_AUDIO_VALUES /
                          (size_t)writer->audio_encoder->ch_layout.nb_channels)
    {
        set_error(error, error_size, "Invalid PCM audio block");
        return 0;
    }
    AVCodecContext *encoder = writer->audio_encoder;
    AVRational time_base = {1, encoder->sample_rate};
    int64_t block_pts = av_rescale_q(pts_us, AV_TIME_BASE_Q, time_base);
    if (writer->have_audio_pts && block_pts != writer->expected_audio_pts)
    {
        set_error(error, error_size, "PCM audio blocks must be contiguous");
        return 0;
    }
    if (frame_count > (size_t)(INT64_MAX - block_pts))
    {
        set_error(error, error_size, "PCM audio timestamp exceeds supported range");
        return 0;
    }
    if (!writer->have_audio_pts)
    {
        writer->next_audio_pts = block_pts;
    }

    int output_capacity = swr_get_out_samples(writer->resampler, (int)frame_count);
    if (output_capacity <= 0 ||
        (size_t)output_capacity > VIDEO_WRITER_MAX_AUDIO_VALUES /
                                      (size_t)encoder->ch_layout.nb_channels)
    {
        set_error(error, error_size, "Converted PCM block exceeds size limits");
        return 0;
    }
    AVFrame *converted = av_frame_alloc();
    if (!converted)
    {
        set_error(error, error_size, "Out of memory converting PCM audio");
        writer->failed = 1;
        return 0;
    }
    converted->format = encoder->sample_fmt;
    converted->sample_rate = encoder->sample_rate;
    converted->nb_samples = output_capacity;
    int result = av_channel_layout_copy(&converted->ch_layout, &encoder->ch_layout);
    if (result >= 0)
        result = av_frame_get_buffer(converted, 0);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot allocate converted PCM block", result);
        av_frame_free(&converted);
        writer->failed = 1;
        return 0;
    }
    const uint8_t *input[] = {(const uint8_t *)samples};
    int converted_samples = swr_convert(writer->resampler,
                                        converted->extended_data,
                                        output_capacity, input,
                                        (int)frame_count);
    if (converted_samples < 0)
    {
        set_av_error(error, error_size, "Cannot convert PCM audio", converted_samples);
        av_frame_free(&converted);
        writer->failed = 1;
        return 0;
    }
    result = av_audio_fifo_realloc(writer->audio_fifo,
                                   av_audio_fifo_size(writer->audio_fifo) +
                                       converted_samples);
    if (result >= 0)
        result = av_audio_fifo_write(writer->audio_fifo,
                                     (void **)converted->extended_data,
                                     converted_samples);
    av_frame_free(&converted);
    if (result != converted_samples)
    {
        if (result < 0)
            set_av_error(error, error_size, "Cannot queue PCM audio", result);
        else
            set_error(error, error_size, "Cannot queue complete PCM audio block");
        writer->failed = 1;
        return 0;
    }
    writer->expected_audio_pts = block_pts + (int64_t)frame_count;
    writer->have_audio_pts = 1;
    if (!encode_audio_fifo(writer, 0, error, error_size))
    {
        writer->failed = 1;
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int video_writer_write_subtitle(VideoWriter *writer, size_t subtitle_index,
                                const char *text, size_t text_size,
                                int64_t start_time_us, int64_t end_time_us,
                                char *error, size_t error_size)
{
    if (!writer || writer->failed || !text || !text_size ||
        text_size > VIDEO_WRITER_MAX_SUBTITLE_TEXT_SIZE ||
        memchr(text, '\0', text_size) ||
        !valid_utf8((const uint8_t *)text, text_size) ||
        subtitle_index >= writer->subtitle_count ||
        start_time_us < 0 || end_time_us <= start_time_us ||
        start_time_us % 1000 != 0 || end_time_us % 1000 != 0)
    {
        set_error(error, error_size, "Invalid subtitle cue");
        return 0;
    }
    VideoWriterSubtitleTrack *track = &writer->subtitles[subtitle_index];
    int64_t pts = start_time_us / 1000;
    int64_t duration = (end_time_us - start_time_us) / 1000;
    if ((track->have_pts && pts < track->last_pts) ||
        duration > UINT_MAX)
    {
        set_error(error, error_size, "Subtitle timestamps must be monotonic and bounded");
        return 0;
    }

    static const char ass_prefix[] = "0,0,Default,,0,0,0,,";
    if (text_size > SIZE_MAX - sizeof(ass_prefix))
    {
        set_error(error, error_size, "Subtitle cue is too large");
        return 0;
    }
    char *ass_text = malloc(sizeof(ass_prefix) + text_size);
    if (!ass_text)
    {
        set_error(error, error_size, "Out of memory copying subtitle cue");
        writer->failed = 1;
        return 0;
    }
    memcpy(ass_text, ass_prefix, sizeof(ass_prefix) - 1);
    memcpy(ass_text + sizeof(ass_prefix) - 1, text, text_size);
    ass_text[sizeof(ass_prefix) - 1 + text_size] = '\0';
    AVSubtitleRect rect = {0};
    rect.type = SUBTITLE_ASS;
    rect.ass = ass_text;
    AVSubtitleRect *rects[] = {&rect};
    AVSubtitle subtitle = {0};
    subtitle.pts = start_time_us;
    subtitle.start_display_time = 0;
    subtitle.end_display_time = (unsigned int)duration;
    subtitle.num_rects = 1;
    subtitle.rects = rects;

    const size_t packet_capacity = VIDEO_WRITER_MAX_SUBTITLE_TEXT_SIZE + 4096;
    uint8_t *packet_data = av_malloc(packet_capacity);
    if (!packet_data)
    {
        free(ass_text);
        set_error(error, error_size, "Out of memory encoding subtitle cue");
        writer->failed = 1;
        return 0;
    }
    int packet_size = avcodec_encode_subtitle(track->encoder, packet_data,
                                              (int)packet_capacity, &subtitle);
    free(ass_text);
    if (packet_size <= 0)
    {
        av_free(packet_data);
        if (packet_size < 0)
            set_av_error(error, error_size, "Cannot encode subtitle cue", packet_size);
        else
            set_error(error, error_size, "Subtitle encoder produced an empty cue");
        writer->failed = 1;
        return 0;
    }
    int result = av_packet_from_data(writer->packet, packet_data, packet_size);
    if (result < 0)
    {
        av_free(packet_data);
        set_av_error(error, error_size, "Cannot allocate encoded subtitle packet", result);
        writer->failed = 1;
        return 0;
    }
    writer->packet->pts = pts;
    writer->packet->dts = pts;
    writer->packet->duration = duration;
    writer->packet->stream_index = track->stream->index;
    result = av_interleaved_write_frame(writer->format, writer->packet);
    av_packet_unref(writer->packet);
    if (result < 0 || writer->io_failed)
    {
        if (writer->io_failed)
            set_error(error, error_size, "Cannot write staged media file");
        else
            set_av_error(error, error_size, "Cannot mux subtitle cue", result);
        writer->failed = 1;
        return 0;
    }
    track->last_pts = pts;
    track->have_pts = 1;
    set_error(error, error_size, "");
    return 1;
}

static int flush_encoder(VideoWriter *writer, AVCodecContext *encoder,
                         AVStream *stream, char *error, size_t error_size)
{
    int result = avcodec_send_frame(encoder, NULL);
    if (result < 0 && result != AVERROR_EOF)
    {
        set_av_error(error, error_size, "Cannot flush media encoder", result);
        return 0;
    }
    return write_packets(writer, encoder, stream, error, error_size);
}

int video_writer_finish(VideoWriter *writer, char *error, size_t error_size)
{
    if (!writer)
    {
        set_error(error, error_size, "Invalid media writer");
        return 0;
    }
    int ok = !writer->failed && writer->header_written && writer->have_video_pts;
    if (!ok && !writer->failed)
        set_error(error, error_size, "Cannot finish an MP4 without video frames");
    if (ok && writer->audio_encoder &&
        !encode_audio_fifo(writer, 1, error, error_size))
        ok = 0;
    if (ok && !flush_encoder(writer, writer->video_encoder, writer->video_stream,
                             error, error_size))
        ok = 0;
    if (ok && writer->audio_encoder &&
        !flush_encoder(writer, writer->audio_encoder, writer->audio_stream,
                       error, error_size))
        ok = 0;
    if (ok)
    {
        int result = av_write_trailer(writer->format);
        avio_flush(writer->io);
        if (result < 0 || writer->io_failed)
        {
            if (writer->io_failed)
                set_error(error, error_size, "Cannot write staged media file");
            else
                set_av_error(error, error_size, "Cannot finalize MP4", result);
            ok = 0;
        }
    }
    if (ok)
        set_error(error, error_size, "");
    else if (!error || !error_size || !error[0])
        set_error(error, error_size, "Media export failed");
    FileContext *files = writer->files;
    int sink_ok = release_writer(writer, ok);
    if (ok && !sink_ok)
    {
        set_error(error, error_size, file_get_error(files));
        if (!error || !error_size || !error[0])
            set_error(error, error_size, "Cannot publish staged media file");
        return 0;
    }
    return ok;
}

void video_writer_close(VideoWriter *writer)
{
    release_writer(writer, 0);
}