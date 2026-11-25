#include "video/video_demux.h"
#include "file/file_wrapper.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define VIDEO_IO_BUFFER_SIZE (32 * 1024)
#define VIDEO_MAX_MEMORY_SIZE (64u * 1024u * 1024u)
#define VIDEO_MAX_PACKET_SIZE (16u * 1024u * 1024u)
#define VIDEO_MAX_FRAME_PIXELS (16u * 1024u * 1024u)
#define VIDEO_MAX_SUBTITLE_TEXT_SIZE (1024u * 1024u)
#define VIDEO_MAX_AUDIO_SAMPLE_VALUES (1024u * 1024u)
#define VIDEO_MAX_TRACKS 64

struct VideoDemux
{
    FileNativeReference file;
    int has_file;
    uint8_t *memory;
    int64_t size;
    int64_t position;
    int io_error;
    int failed;
    AVIOContext *io;
    AVFormatContext *format;
    AVPacket *packet;
    AVCodecContext *decoder;
    AVCodecContext *audio_decoder;
    AVCodecContext *subtitle_decoder;
    AVFrame *decoded;
    struct SwsContext *scaler;
    SwrContext *audio_resampler;
    uint8_t *rgba;
    size_t rgba_capacity;
    float *audio_samples;
    size_t audio_samples_capacity;
    char *subtitle_text;
    size_t subtitle_text_capacity;
    int decoder_track;
    int audio_track;
    int subtitle_track;
    int read_started;
    int demux_eof;
    int decoder_flushed;
    int audio_flushed;
    int subtitle_flushed;
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

static VideoColorValue video_color_value(int value, int unspecified_value,
                                         int bt709_value)
{
    if (value == unspecified_value)
        return VIDEO_COLOR_UNKNOWN;
    return value == bt709_value ? VIDEO_COLOR_BT709 : VIDEO_COLOR_UNSUPPORTED;
}

static VideoColorRange video_color_range(int value)
{
    if (value == AVCOL_RANGE_UNSPECIFIED)
        return VIDEO_COLOR_RANGE_UNKNOWN;
    if (value == AVCOL_RANGE_MPEG)
        return VIDEO_COLOR_RANGE_LIMITED;
    if (value == AVCOL_RANGE_JPEG)
        return VIDEO_COLOR_RANGE_FULL;
    return VIDEO_COLOR_RANGE_UNSUPPORTED;
}

static int effective_color_value(int frame_value, int stream_value,
                                 int unspecified_value)
{
    return frame_value == unspecified_value ? stream_value : frame_value;
}

static int read_source(void *opaque, uint8_t *buffer, int length)
{
    VideoDemux *demux = opaque;
    if (length <= 0)
        return AVERROR(EINVAL);
    if (demux->position >= demux->size)
        return AVERROR_EOF;
    int64_t remaining = demux->size - demux->position;
    if (length > remaining)
        length = (int)remaining;
    int64_t count;
    if (demux->has_file)
        count = file_native_read(&demux->file, (uint64_t)demux->position, buffer, (size_t)length);
    else
    {
        memcpy(buffer, demux->memory + (size_t)demux->position, (size_t)length);
        count = length;
    }
    if (count <= 0)
    {
        demux->io_error = 1;
        return AVERROR(EIO);
    }
    demux->position += count;
    return (int)count;
}

static int64_t seek_source(void *opaque, int64_t offset, int whence)
{
    VideoDemux *demux = opaque;
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE)
        return demux->size;
    int64_t base;
    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = demux->position;
    else if (whence == SEEK_END)
        base = demux->size;
    else
        return AVERROR(EINVAL);
    if ((offset < 0 && offset < -base) ||
        (offset > 0 && offset > demux->size - base))
        return AVERROR(EINVAL);
    demux->position = base + offset;
    return demux->position;
}

static int reject_external_io(AVFormatContext *format, AVIOContext **io,
                              const char *url, int flags, AVDictionary **options)
{
    (void)format;
    (void)io;
    (void)url;
    (void)flags;
    (void)options;
    return AVERROR(EPERM);
}

void video_demux_close(VideoDemux *demux)
{
    if (!demux)
        return;
    av_freep(&demux->rgba);
    av_freep(&demux->audio_samples);
    free(demux->subtitle_text);
    sws_freeContext(demux->scaler);
    swr_free(&demux->audio_resampler);
    av_frame_free(&demux->decoded);
    avcodec_free_context(&demux->subtitle_decoder);
    avcodec_free_context(&demux->audio_decoder);
    avcodec_free_context(&demux->decoder);
    av_packet_free(&demux->packet);
    avformat_close_input(&demux->format);
    if (demux->io)
    {
        av_freep(&demux->io->buffer);
        avio_context_free(&demux->io);
    }
    if (demux->has_file)
        file_native_close(&demux->file);
    free(demux->memory);
    free(demux);
}

static VideoDemux *open_source(VideoDemux *demux, char *error, size_t error_size)
{
    uint8_t *buffer = av_malloc(VIDEO_IO_BUFFER_SIZE);
    if (!buffer)
    {
        set_error(error, error_size, "Out of memory allocating media input buffer");
        goto fail;
    }
    demux->io = avio_alloc_context(buffer, VIDEO_IO_BUFFER_SIZE, 0, demux,
                                   read_source, NULL, seek_source);
    if (!demux->io)
    {
        av_free(buffer);
        set_error(error, error_size, "Out of memory allocating media input");
        goto fail;
    }
    demux->format = avformat_alloc_context();
    if (!demux->format)
    {
        set_error(error, error_size, "Out of memory allocating media demuxer");
        goto fail;
    }
    demux->format->pb = demux->io;
    demux->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    demux->format->io_open = reject_external_io;
    demux->format->max_streams = VIDEO_MAX_TRACKS;
    const AVInputFormat *format = av_find_input_format("mov");
    if (!format)
    {
        set_error(error, error_size, "FFmpeg MP4/MOV demuxer is unavailable");
        goto fail;
    }
    int result = avformat_open_input(&demux->format, NULL, format, NULL);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot open MP4/MOV", result);
        goto fail;
    }
    result = avformat_find_stream_info(demux->format, NULL);
    if (result < 0 || demux->io_error)
    {
        set_av_error(error, error_size, "Cannot inspect media tracks",
                     demux->io_error ? AVERROR(EIO) : result);
        goto fail;
    }
    demux->packet = av_packet_alloc();
    if (!demux->packet)
    {
        set_error(error, error_size, "Out of memory allocating media packet");
        goto fail;
    }
    set_error(error, error_size, "");
    return demux;
fail:
    video_demux_close(demux);
    return NULL;
}

VideoDemux *video_demux_open_file(FileContext *files, const char *path,
                                  char *error, size_t error_size)
{
    if (!files || !path || !file_virtual_path_is_valid(path, 0, 0))
    {
        set_error(error, error_size, "Invalid virtual media path");
        return NULL;
    }
    VideoDemux *demux = calloc(1, sizeof(*demux));
    if (!demux)
    {
        set_error(error, error_size, "Out of memory allocating media source");
        return NULL;
    }
    if (!file_native_open(files, path, &demux->file))
    {
        set_error(error, error_size, file_get_error(files));
        free(demux);
        return NULL;
    }
    demux->has_file = 1;
    if (demux->file.size > INT64_MAX)
    {
        set_error(error, error_size, "Media source is too large");
        video_demux_close(demux);
        return NULL;
    }
    demux->size = (int64_t)demux->file.size;
    return open_source(demux, error, error_size);
}

VideoDemux *video_demux_open_memory(const uint8_t *data, size_t size,
                                    char *error, size_t error_size)
{
    if (!data || !size || size > VIDEO_MAX_MEMORY_SIZE)
    {
        set_error(error, error_size, "Media buffer must contain 1 byte to 64 MiB");
        return NULL;
    }
    VideoDemux *demux = calloc(1, sizeof(*demux));
    if (!demux)
    {
        set_error(error, error_size, "Out of memory allocating media source");
        return NULL;
    }
    demux->memory = malloc(size);
    if (!demux->memory)
    {
        set_error(error, error_size, "Out of memory copying media source");
        free(demux);
        return NULL;
    }
    memcpy(demux->memory, data, size);
    demux->size = (int64_t)size;
    return open_source(demux, error, error_size);
}

int video_demux_track_count(const VideoDemux *demux)
{
    return demux ? (int)demux->format->nb_streams : 0;
}

int video_demux_track(const VideoDemux *demux, int index, VideoTrack *track)
{
    if (!demux || !track || index < 0 || index >= video_demux_track_count(demux))
        return 0;
    const AVStream *stream = demux->format->streams[index];
    memset(track, 0, sizeof(*track));
    switch (stream->codecpar->codec_type)
    {
    case AVMEDIA_TYPE_VIDEO: track->type = VIDEO_TRACK_VIDEO; break;
    case AVMEDIA_TYPE_AUDIO: track->type = VIDEO_TRACK_AUDIO; break;
    case AVMEDIA_TYPE_SUBTITLE: track->type = VIDEO_TRACK_SUBTITLE; break;
    case AVMEDIA_TYPE_DATA: track->type = VIDEO_TRACK_DATA; break;
    case AVMEDIA_TYPE_ATTACHMENT: track->type = VIDEO_TRACK_ATTACHMENT; break;
    default: track->type = VIDEO_TRACK_UNKNOWN; break;
    }
    snprintf(track->codec, sizeof(track->codec), "%s",
             avcodec_get_name(stream->codecpar->codec_id));
    const AVDictionaryEntry *language = av_dict_get(stream->metadata, "language", NULL, 0);
    if (language && language->value[0])
    {
        snprintf(track->language, sizeof(track->language), "%s", language->value);
        track->metadata |= VIDEO_TRACK_HAS_LANGUAGE;
    }
    const AVDictionaryEntry *title = av_dict_get(stream->metadata, "title", NULL, 0);
    if (!title || !title->value[0])
        title = av_dict_get(stream->metadata, "name", NULL, 0);
    if (title && title->value[0])
    {
        snprintf(track->title, sizeof(track->title), "%s", title->value);
        track->metadata |= VIDEO_TRACK_HAS_TITLE;
    }
    track->time_base_num = stream->time_base.num;
    track->time_base_den = stream->time_base.den;
    track->start_time = stream->start_time;
    track->duration = stream->duration;
    if (stream->start_time != AV_NOPTS_VALUE)
        track->metadata |= VIDEO_TRACK_HAS_START_TIME;
    if (stream->duration != AV_NOPTS_VALUE)
        track->metadata |= VIDEO_TRACK_HAS_DURATION;
    track->width = stream->codecpar->width;
    track->height = stream->codecpar->height;
    if (track->type == VIDEO_TRACK_VIDEO && track->width > 0 && track->height > 0)
        track->metadata |= VIDEO_TRACK_HAS_DIMENSIONS;
    if (track->type == VIDEO_TRACK_VIDEO)
    {
        AVRational frame_rate = stream->avg_frame_rate;
        if (frame_rate.num > 0 && frame_rate.den > 0)
        {
            track->frame_rate_num = frame_rate.num;
            track->frame_rate_den = frame_rate.den;
            track->metadata |= VIDEO_TRACK_HAS_FRAME_RATE;
        }

        AVRational sample_aspect_ratio = stream->codecpar->sample_aspect_ratio;
        if (sample_aspect_ratio.num <= 0 || sample_aspect_ratio.den <= 0)
            sample_aspect_ratio = stream->sample_aspect_ratio;
        if (sample_aspect_ratio.num > 0 && sample_aspect_ratio.den > 0)
        {
            track->sample_aspect_ratio_num = sample_aspect_ratio.num;
            track->sample_aspect_ratio_den = sample_aspect_ratio.den;
            track->metadata |= VIDEO_TRACK_HAS_SAMPLE_ASPECT_RATIO;
        }

        track->color_primaries = video_color_value(
            stream->codecpar->color_primaries, AVCOL_PRI_UNSPECIFIED,
            AVCOL_PRI_BT709);
        track->color_transfer = video_color_value(
            stream->codecpar->color_trc, AVCOL_TRC_UNSPECIFIED,
            AVCOL_TRC_BT709);
        track->color_matrix = video_color_value(
            stream->codecpar->color_space, AVCOL_SPC_UNSPECIFIED,
            AVCOL_SPC_BT709);
        track->color_range = video_color_range(stream->codecpar->color_range);

        track->rotation_degrees = 0;
        const uint8_t *display_matrix_data = NULL;
        size_t display_matrix_size = 0;
#if LIBAVCODEC_VERSION_MAJOR >= 61
        for (int i = 0; i < stream->codecpar->nb_coded_side_data; ++i)
        {
            const AVPacketSideData *side_data =
                &stream->codecpar->coded_side_data[i];
            if (side_data->type == AV_PKT_DATA_DISPLAYMATRIX)
            {
                display_matrix_data = side_data->data;
                display_matrix_size = side_data->size;
                break;
            }
        }
#else
        for (int i = 0; i < stream->nb_side_data; ++i)
        {
            const AVPacketSideData *side_data = &stream->side_data[i];
            if (side_data->type == AV_PKT_DATA_DISPLAYMATRIX)
            {
                display_matrix_data = side_data->data;
                display_matrix_size = side_data->size;
                break;
            }
        }
#endif
        if (display_matrix_data &&
            display_matrix_size >= 9 * sizeof(int32_t))
        {
            double rotation = av_display_rotation_get(
                (const int32_t *)display_matrix_data);
            if (isfinite(rotation) && rotation >= -360.0 && rotation <= 360.0)
            {
                track->rotation_degrees = (int)lround(rotation);
                track->metadata |= VIDEO_TRACK_HAS_ROTATION;
            }
        }
        else if (!display_matrix_data)
        {
            track->metadata |= VIDEO_TRACK_HAS_ROTATION;
        }
    }
    if (track->type == VIDEO_TRACK_AUDIO)
    {
        track->sample_rate = stream->codecpar->sample_rate;
        track->channels = stream->codecpar->ch_layout.nb_channels;
        if (track->channels > 0)
            av_channel_layout_describe(&stream->codecpar->ch_layout,
                                       track->channel_layout,
                                       sizeof(track->channel_layout));
    }
    track->disposition_default = !!(stream->disposition & AV_DISPOSITION_DEFAULT);
    track->disposition_forced = !!(stream->disposition & AV_DISPOSITION_FORCED);
    return 1;
}

int video_demux_next(VideoDemux *demux, VideoPacket *packet,
                     char *error, size_t error_size)
{
    if (!demux || !packet)
    {
        set_error(error, error_size, "Invalid media packet request");
        return -1;
    }
    if (demux->decoder || demux->audio_decoder || demux->subtitle_decoder)
    {
        set_error(error, error_size, "Packet reads are unavailable after opening a decoder");
        return -1;
    }
    if (demux->failed)
    {
        set_error(error, error_size, "Media source is in a failed state");
        return -1;
    }
    demux->read_started = 1;
    av_packet_unref(demux->packet);
    int result = av_read_frame(demux->format, demux->packet);
    if (demux->io_error)
    {
        demux->failed = 1;
        set_error(error, error_size, "Cannot read media source");
        return -1;
    }
    if (result == AVERROR_EOF)
    {
        set_error(error, error_size, "");
        return 0;
    }
    if (result < 0)
    {
        demux->failed = 1;
        set_av_error(error, error_size, "Cannot read media packet", result);
        return -1;
    }
    if (demux->packet->size < 0 || (size_t)demux->packet->size > VIDEO_MAX_PACKET_SIZE)
    {
        demux->failed = 1;
        av_packet_unref(demux->packet);
        set_error(error, error_size, "Media packet exceeds 16 MiB");
        return -1;
    }
    packet->track = demux->packet->stream_index;
    packet->data = demux->packet->data;
    packet->size = (size_t)demux->packet->size;
    packet->pts = demux->packet->pts;
    packet->dts = demux->packet->dts;
    packet->duration = demux->packet->duration;
    packet->keyframe = !!(demux->packet->flags & AV_PKT_FLAG_KEY);
    set_error(error, error_size, "");
    return 1;
}

int video_demux_decode_open(VideoDemux *demux, int track,
                            char *error, size_t error_size)
{
    if (!demux || !demux->format || demux->read_started || demux->decoder ||
        demux->audio_decoder || demux->subtitle_decoder ||
        track < 0 || track >= video_demux_track_count(demux))
    {
        set_error(error, error_size, "Invalid video decoder request");
        return 0;
    }
    AVStream *stream = demux->format->streams[track];
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    {
        set_error(error, error_size, "Selected track is not video");
        return 0;
    }
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        set_error(error, error_size, "No decoder is available for the video track");
        return 0;
    }
    demux->decoder = avcodec_alloc_context3(codec);
    if (!demux->decoder)
    {
        set_error(error, error_size, "Out of memory allocating video decoder");
        return 0;
    }
    int result = avcodec_parameters_to_context(demux->decoder, stream->codecpar);
    if (result >= 0)
        result = avcodec_open2(demux->decoder, codec, NULL);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot open video decoder", result);
        avcodec_free_context(&demux->decoder);
        return 0;
    }
    demux->decoded = av_frame_alloc();
    if (!demux->decoded)
    {
        set_error(error, error_size, "Out of memory allocating decoded video frame");
        avcodec_free_context(&demux->decoder);
        return 0;
    }
    demux->decoder_track = track;
    demux->read_started = 1;
    set_error(error, error_size, "");
    return 1;
}

static int decode_frame_to_rgba(VideoDemux *demux, VideoFrame *frame,
                                char *error, size_t error_size)
{
    AVFrame *decoded = demux->decoded;
    const AVCodecParameters *codecpar =
        demux->format->streams[demux->decoder_track]->codecpar;
    int primaries_value = effective_color_value(
        decoded->color_primaries, codecpar->color_primaries,
        AVCOL_PRI_UNSPECIFIED);
    int transfer_value = effective_color_value(
        decoded->color_trc, codecpar->color_trc, AVCOL_TRC_UNSPECIFIED);
    int matrix_value = effective_color_value(
        decoded->colorspace, codecpar->color_space, AVCOL_SPC_UNSPECIFIED);
    int range_value = effective_color_value(
        decoded->color_range, codecpar->color_range, AVCOL_RANGE_UNSPECIFIED);
    VideoColorValue primaries = video_color_value(
        primaries_value, AVCOL_PRI_UNSPECIFIED, AVCOL_PRI_BT709);
    VideoColorValue transfer = video_color_value(
        transfer_value, AVCOL_TRC_UNSPECIFIED, AVCOL_TRC_BT709);
    VideoColorValue matrix = video_color_value(
        matrix_value, AVCOL_SPC_UNSPECIFIED, AVCOL_SPC_BT709);
    VideoColorRange range = video_color_range(range_value);
    if (primaries != VIDEO_COLOR_BT709 || transfer != VIDEO_COLOR_BT709 ||
        matrix != VIDEO_COLOR_BT709 ||
        (range != VIDEO_COLOR_RANGE_LIMITED && range != VIDEO_COLOR_RANGE_FULL))
    {
        if (error && error_size)
            snprintf(error, error_size,
                     "Unsupported SDR color metadata (primaries=%d, transfer=%d, "
                     "matrix=%d, range=%d)",
                     primaries, transfer, matrix, range);
        return -1;
    }
    if (decoded->width <= 0 || decoded->height <= 0 ||
        (uint64_t)decoded->width * (uint64_t)decoded->height >
            VIDEO_MAX_FRAME_PIXELS ||
        decoded->width > INT_MAX / 4)
    {
        set_error(error, error_size, "Decoded video frame exceeds size limits");
        return -1;
    }
    int stride = decoded->width * 4;
    size_t required = (size_t)stride * (size_t)decoded->height;
    if (required > demux->rgba_capacity)
    {
        uint8_t *rgba = av_realloc(demux->rgba, required);
        if (!rgba)
        {
            set_error(error, error_size, "Out of memory allocating decoded video frame");
            return -1;
        }
        demux->rgba = rgba;
        demux->rgba_capacity = required;
    }

    demux->scaler = sws_getCachedContext(
        demux->scaler, decoded->width, decoded->height,
        (enum AVPixelFormat)decoded->format, decoded->width, decoded->height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!demux->scaler)
    {
        set_error(error, error_size, "Cannot configure video pixel conversion");
        return -1;
    }
    const int *coefficients = sws_getCoefficients(SWS_CS_ITU709);
    int source_range = range == VIDEO_COLOR_RANGE_FULL;
    int result = sws_setColorspaceDetails(demux->scaler, coefficients, source_range,
                                         coefficients, 1, 0, 1 << 16, 1 << 16);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot configure video color conversion", result);
        return -1;
    }
    uint8_t *destination[] = { demux->rgba, NULL, NULL, NULL };
    int destination_stride[] = { stride, 0, 0, 0 };
    result = sws_scale(demux->scaler,
                       (const uint8_t *const *)decoded->data, decoded->linesize,
                       0, decoded->height, destination, destination_stride);
    if (result != decoded->height)
    {
        set_error(error, error_size, "Cannot convert decoded video frame");
        return -1;
    }
#if LIBAVUTIL_VERSION_MAJOR >= 58
    int64_t duration = decoded->duration;
#else
    int64_t duration = decoded->pkt_duration;
#endif
    frame->rgba = demux->rgba;
    frame->size = required;
    frame->width = decoded->width;
    frame->height = decoded->height;
    frame->stride = stride;
    frame->pts = decoded->best_effort_timestamp;
    frame->duration = duration;
    AVRational time_base = demux->format->streams[demux->decoder_track]->time_base;
    frame->time_base_num = time_base.num;
    frame->time_base_den = time_base.den;
    AVRational sample_aspect_ratio = decoded->sample_aspect_ratio;
    if (sample_aspect_ratio.num <= 0 || sample_aspect_ratio.den <= 0)
        sample_aspect_ratio = demux->decoder->sample_aspect_ratio;
    frame->sample_aspect_ratio_num = sample_aspect_ratio.num;
    frame->sample_aspect_ratio_den = sample_aspect_ratio.den;
    frame->color_primaries = primaries;
    frame->color_transfer = transfer;
    frame->color_matrix = matrix;
    frame->color_range = range;
#if LIBAVUTIL_VERSION_MAJOR >= 58
    frame->interlaced = (decoded->flags & AV_FRAME_FLAG_INTERLACED) != 0;
#else
    frame->interlaced = decoded->interlaced_frame != 0;
#endif
    return 1;
}

int video_demux_decode_next(VideoDemux *demux, VideoFrame *frame,
                            char *error, size_t error_size)
{
    if (!demux || !frame || !demux->decoder)
    {
        set_error(error, error_size, "Video decoder is not open");
        return -1;
    }
    if (demux->failed)
    {
        set_error(error, error_size, "Media source is in a failed state");
        return -1;
    }

    for (;;)
    {
        av_frame_unref(demux->decoded);
        int result = avcodec_receive_frame(demux->decoder, demux->decoded);
        if (result == 0)
        {
            result = decode_frame_to_rgba(demux, frame, error, error_size);
            if (result < 0)
                demux->failed = 1;
            else
                set_error(error, error_size, "");
            return result;
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot decode video frame", result);
            return -1;
        }
        if (result == AVERROR_EOF)
        {
            set_error(error, error_size, "");
            return 0;
        }
        if (demux->demux_eof)
        {
            if (!demux->decoder_flushed)
            {
                result = avcodec_send_packet(demux->decoder, NULL);
                if (result < 0 && result != AVERROR_EOF)
                {
                    demux->failed = 1;
                    set_av_error(error, error_size, "Cannot flush video decoder", result);
                    return -1;
                }
                demux->decoder_flushed = 1;
                continue;
            }
            set_error(error, error_size, "");
            return 0;
        }

        av_packet_unref(demux->packet);
        result = av_read_frame(demux->format, demux->packet);
        if (demux->io_error)
        {
            demux->failed = 1;
            set_error(error, error_size, "Cannot read media source");
            return -1;
        }
        if (result == AVERROR_EOF)
        {
            demux->demux_eof = 1;
            continue;
        }
        if (result < 0)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot read media packet", result);
            return -1;
        }
        if (demux->packet->size < 0 ||
            (size_t)demux->packet->size > VIDEO_MAX_PACKET_SIZE)
        {
            demux->failed = 1;
            set_error(error, error_size, "Media packet exceeds 16 MiB");
            return -1;
        }
        if (demux->packet->stream_index != demux->decoder_track)
            continue;
        result = avcodec_send_packet(demux->decoder, demux->packet);
        if (result < 0)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot submit video packet", result);
            return -1;
        }
    }
}

int video_demux_seek_time(VideoDemux *demux, int64_t timestamp,
                          VideoFrame *frame, char *error, size_t error_size)
{
    if (!demux || !frame || !demux->decoder || demux->decoder_track < 0 ||
        demux->decoder_track >= video_demux_track_count(demux))
    {
        set_error(error, error_size, "Video decoder is not open");
        return -1;
    }
    if (demux->io_error)
    {
        set_error(error, error_size, "Cannot seek media source after an I/O error");
        return -1;
    }

    int result = av_seek_frame(demux->format, demux->decoder_track, timestamp,
                               AVSEEK_FLAG_BACKWARD);
    if (result < 0)
    {
        demux->failed = 1;
        set_av_error(error, error_size, "Cannot seek media source", result);
        return -1;
    }

    avformat_flush(demux->format);
    avcodec_flush_buffers(demux->decoder);
    av_packet_unref(demux->packet);
    demux->failed = 0;
    demux->demux_eof = 0;
    demux->decoder_flushed = 0;

    for (;;)
    {
        result = video_demux_decode_next(demux, frame, error, error_size);
        if (result != 1)
            return result;
        if (frame->pts != INT64_MIN && frame->pts >= timestamp)
            return 1;
    }
}

int video_demux_decode_rewind(VideoDemux *demux, char *error,
                              size_t error_size)
{
    if (!demux || !demux->decoder || demux->decoder_track < 0 ||
        demux->decoder_track >= video_demux_track_count(demux))
    {
        set_error(error, error_size, "Video decoder is not open");
        return 0;
    }
    if (demux->io_error)
    {
        set_error(error, error_size, "Cannot seek media source after an I/O error");
        return 0;
    }

    AVStream *stream = demux->format->streams[demux->decoder_track];
    if (stream->start_time == AV_NOPTS_VALUE)
    {
        set_error(error, error_size, "Cannot seek by frame without a known video start time");
        return 0;
    }

    int result = av_seek_frame(demux->format, demux->decoder_track,
                               stream->start_time, AVSEEK_FLAG_BACKWARD);
    if (result < 0)
    {
        demux->failed = 1;
        set_av_error(error, error_size, "Cannot seek media source to its start", result);
        return 0;
    }

    avformat_flush(demux->format);
    avcodec_flush_buffers(demux->decoder);
    av_packet_unref(demux->packet);
    demux->failed = 0;
    demux->demux_eof = 0;
    demux->decoder_flushed = 0;
    set_error(error, error_size, "");
    return 1;
}

int video_demux_seek_frame(VideoDemux *demux, uint64_t ordinal,
                           VideoFrame *frame, char *error, size_t error_size)
{
    if (!frame)
    {
        set_error(error, error_size, "Invalid video frame output");
        return -1;
    }
    if (!video_demux_decode_rewind(demux, error, error_size))
        return -1;

    AVStream *stream = demux->format->streams[demux->decoder_track];
    int result;
    uint64_t current_ordinal = 0;
    int64_t previous_timestamp = INT64_MIN;
    for (;;)
    {
        result = video_demux_decode_next(demux, frame, error, error_size);
        if (result != 1)
            return result;
        if (frame->pts == INT64_MIN)
        {
            demux->failed = 1;
            set_error(error, error_size,
                      "Cannot seek by frame when a decoded frame has no timestamp");
            return -1;
        }
        if (frame->pts < stream->start_time)
            continue;
        if (previous_timestamp != INT64_MIN && frame->pts < previous_timestamp)
        {
            demux->failed = 1;
            set_error(error, error_size,
                      "Cannot seek by frame when presentation timestamps are not ordered");
            return -1;
        }
        previous_timestamp = frame->pts;
        if (current_ordinal == ordinal)
        {
            set_error(error, error_size, "");
            return 1;
        }
        if (current_ordinal == UINT64_MAX)
        {
            demux->failed = 1;
            set_error(error, error_size, "Decoded frame ordinal exceeds supported range");
            return -1;
        }
        current_ordinal++;
    }
}

int video_demux_audio_open(VideoDemux *demux, int track,
                           char *error, size_t error_size)
{
    if (!demux || !demux->format || demux->read_started || demux->decoder ||
        demux->audio_decoder || demux->subtitle_decoder || track < 0 ||
        track >= video_demux_track_count(demux))
    {
        set_error(error, error_size, "Invalid audio decoder request");
        return 0;
    }
    AVStream *stream = demux->format->streams[track];
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    {
        set_error(error, error_size, "Selected track is not audio");
        return 0;
    }
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        set_error(error, error_size, "No decoder is available for the audio track");
        return 0;
    }
    demux->audio_decoder = avcodec_alloc_context3(codec);
    if (!demux->audio_decoder)
    {
        set_error(error, error_size, "Out of memory allocating audio decoder");
        return 0;
    }
    int result = avcodec_parameters_to_context(demux->audio_decoder,
                                               stream->codecpar);
    if (result >= 0)
        result = avcodec_open2(demux->audio_decoder, codec, NULL);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot open audio decoder", result);
        avcodec_free_context(&demux->audio_decoder);
        return 0;
    }
    AVChannelLayout *layout = &demux->audio_decoder->ch_layout;
    int channels = layout->nb_channels;
    int sample_rate = demux->audio_decoder->sample_rate;
    if (channels <= 0 || channels > 32 || sample_rate <= 0 ||
        !av_channel_layout_check(layout))
    {
        set_error(error, error_size, "Audio track has an unsupported sample layout");
        avcodec_free_context(&demux->audio_decoder);
        return 0;
    }
    result = swr_alloc_set_opts2(&demux->audio_resampler,
                                 layout, AV_SAMPLE_FMT_FLT, sample_rate,
                                 layout, demux->audio_decoder->sample_fmt,
                                 sample_rate, 0, NULL);
    if (result >= 0)
        result = swr_init(demux->audio_resampler);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot configure audio conversion", result);
        swr_free(&demux->audio_resampler);
        avcodec_free_context(&demux->audio_decoder);
        return 0;
    }
    demux->decoded = av_frame_alloc();
    if (!demux->decoded)
    {
        set_error(error, error_size, "Out of memory allocating decoded audio frame");
        swr_free(&demux->audio_resampler);
        avcodec_free_context(&demux->audio_decoder);
        return 0;
    }
    demux->audio_track = track;
    demux->read_started = 1;
    set_error(error, error_size, "");
    return 1;
}

static int decode_audio_frame(VideoDemux *demux, VideoAudioBlock *block,
                              char *error, size_t error_size)
{
    AVFrame *decoded = demux->decoded;
    int channels = demux->audio_decoder->ch_layout.nb_channels;
    if (decoded->sample_rate != demux->audio_decoder->sample_rate ||
        decoded->format != demux->audio_decoder->sample_fmt ||
        decoded->ch_layout.nb_channels != channels ||
        !av_channel_layout_check(&decoded->ch_layout) ||
        av_channel_layout_compare(&decoded->ch_layout,
                                  &demux->audio_decoder->ch_layout) != 0)
    {
        set_error(error, error_size, "Audio format changed during decode");
        return -1;
    }
    int output_capacity = swr_get_out_samples(demux->audio_resampler,
                                              decoded->nb_samples);
    if (decoded->nb_samples <= 0 || output_capacity <= 0 ||
        (size_t)output_capacity > VIDEO_MAX_AUDIO_SAMPLE_VALUES / (size_t)channels)
    {
        set_error(error, error_size, "Decoded audio block exceeds size limits");
        return -1;
    }
    size_t required = (size_t)output_capacity * (size_t)channels;
    if (required > demux->audio_samples_capacity)
    {
        float *samples = av_realloc_array(demux->audio_samples, required,
                                          sizeof(*samples));
        if (!samples)
        {
            set_error(error, error_size, "Out of memory allocating decoded audio block");
            return -1;
        }
        demux->audio_samples = samples;
        demux->audio_samples_capacity = required;
    }
    uint8_t *output[] = {(uint8_t *)demux->audio_samples};
    int sample_count = swr_convert(demux->audio_resampler, output,
                                   output_capacity,
                                   (const uint8_t **)decoded->extended_data,
                                   decoded->nb_samples);
    if (sample_count < 0)
    {
        set_av_error(error, error_size, "Cannot convert decoded audio", sample_count);
        return -1;
    }
    AVRational time_base = demux->format->streams[demux->audio_track]->time_base;
    block->samples = demux->audio_samples;
    block->frame_count = (size_t)sample_count;
    block->sample_rate = demux->audio_decoder->sample_rate;
    block->channels = channels;
    block->pts_us = decoded->best_effort_timestamp == AV_NOPTS_VALUE
                        ? INT64_MIN
                        : av_rescale_q(decoded->best_effort_timestamp,
                                       time_base, AV_TIME_BASE_Q);
    block->duration_us = av_rescale_q(sample_count,
                                      (AVRational){1, block->sample_rate},
                                      AV_TIME_BASE_Q);
    return sample_count == 0 ? 0 : 1;
}

int video_demux_audio_next(VideoDemux *demux, VideoAudioBlock *block,
                           char *error, size_t error_size)
{
    if (!demux || !block || !demux->audio_decoder)
    {
        set_error(error, error_size, "Audio decoder is not open");
        return -1;
    }
    if (demux->failed)
    {
        set_error(error, error_size, "Media source is in a failed state");
        return -1;
    }
    for (;;)
    {
        av_frame_unref(demux->decoded);
        int result = avcodec_receive_frame(demux->audio_decoder, demux->decoded);
        if (result == 0)
        {
            result = decode_audio_frame(demux, block, error, error_size);
            if (result < 0)
                demux->failed = 1;
            else if (result > 0)
                set_error(error, error_size, "");
            if (result != 0)
                return result;
            continue;
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot decode audio block", result);
            return -1;
        }
        if (result == AVERROR_EOF)
        {
            set_error(error, error_size, "");
            return 0;
        }
        if (demux->demux_eof)
        {
            if (!demux->audio_flushed)
            {
                result = avcodec_send_packet(demux->audio_decoder, NULL);
                if (result < 0 && result != AVERROR_EOF)
                {
                    demux->failed = 1;
                    set_av_error(error, error_size, "Cannot flush audio decoder", result);
                    return -1;
                }
                demux->audio_flushed = 1;
                continue;
            }
            set_error(error, error_size, "");
            return 0;
        }
        av_packet_unref(demux->packet);
        result = av_read_frame(demux->format, demux->packet);
        if (demux->io_error)
        {
            demux->failed = 1;
            set_error(error, error_size, "Cannot read media source");
            return -1;
        }
        if (result == AVERROR_EOF)
        {
            demux->demux_eof = 1;
            continue;
        }
        if (result < 0)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot read media packet", result);
            return -1;
        }
        if (demux->packet->size < 0 ||
            (size_t)demux->packet->size > VIDEO_MAX_PACKET_SIZE)
        {
            demux->failed = 1;
            set_error(error, error_size, "Media packet exceeds 16 MiB");
            return -1;
        }
        if (demux->packet->stream_index != demux->audio_track)
            continue;
        result = avcodec_send_packet(demux->audio_decoder, demux->packet);
        if (result < 0)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot submit audio packet", result);
            return -1;
        }
    }
}

int video_demux_subtitle_open(VideoDemux *demux, int track,
                              char *error, size_t error_size)
{
    if (!demux || !demux->format || demux->read_started || demux->decoder ||
        demux->audio_decoder || demux->subtitle_decoder || track < 0 ||
        track >= video_demux_track_count(demux))
    {
        set_error(error, error_size, "Invalid subtitle decoder request");
        return 0;
    }
    AVStream *stream = demux->format->streams[track];
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE ||
        stream->codecpar->codec_id != AV_CODEC_ID_MOV_TEXT)
    {
        set_error(error, error_size, "Selected track is not mov_text subtitles");
        return 0;
    }
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        set_error(error, error_size, "No decoder is available for the subtitle track");
        return 0;
    }
    demux->subtitle_decoder = avcodec_alloc_context3(codec);
    if (!demux->subtitle_decoder)
    {
        set_error(error, error_size, "Out of memory allocating subtitle decoder");
        return 0;
    }
    int result = avcodec_parameters_to_context(demux->subtitle_decoder,
                                               stream->codecpar);
    if (result >= 0)
        result = avcodec_open2(demux->subtitle_decoder, codec, NULL);
    if (result < 0)
    {
        set_av_error(error, error_size, "Cannot open subtitle decoder", result);
        avcodec_free_context(&demux->subtitle_decoder);
        return 0;
    }
    demux->subtitle_track = track;
    demux->read_started = 1;
    set_error(error, error_size, "");
    return 1;
}

static int copy_subtitle_text(VideoDemux *demux, const char *text,
                              size_t *text_size, char *error,
                              size_t error_size)
{
    size_t length = 0;
    while (length <= VIDEO_MAX_SUBTITLE_TEXT_SIZE && text[length])
        length++;
    if (length > VIDEO_MAX_SUBTITLE_TEXT_SIZE)
    {
        set_error(error, error_size, "Subtitle text exceeds 1 MiB");
        return 0;
    }
    if (length + 1 > demux->subtitle_text_capacity)
    {
        char *new_text = realloc(demux->subtitle_text, length + 1);
        if (!new_text)
        {
            set_error(error, error_size, "Out of memory copying subtitle text");
            return 0;
        }
        demux->subtitle_text = new_text;
        demux->subtitle_text_capacity = length + 1;
    }
    memcpy(demux->subtitle_text, text, length + 1);
    *text_size = length;
    return 1;
}

static int subtitle_time_us(const AVSubtitle *decoded, const AVPacket *packet,
                            AVRational time_base, int64_t offset_ms,
                            int64_t *time_us)
{
    int64_t base = decoded->pts;
    if (base == AV_NOPTS_VALUE)
    {
        if (packet->pts == AV_NOPTS_VALUE)
            return 0;
        base = av_rescale_q(packet->pts, time_base, AV_TIME_BASE_Q);
    }
    int64_t offset_us = offset_ms * 1000;
    if (base > INT64_MAX - offset_us)
        return 0;
    *time_us = base + offset_us;
    return 1;
}

static const char *subtitle_rect_text(const AVSubtitleRect *rect)
{
    if (rect->type == SUBTITLE_TEXT)
        return rect->text;
    if (rect->type != SUBTITLE_ASS || !rect->ass)
        return NULL;

    const char *text = rect->ass;
    for (int field = 0; field < 8; ++field)
    {
        text = strchr(text, ',');
        if (!text)
            return NULL;
        text++;
    }
    return text;
}

static int decode_subtitle_packet(VideoDemux *demux, const AVPacket *packet,
                                  VideoSubtitle *subtitle, char *error,
                                  size_t error_size)
{
    AVSubtitle decoded = {0};
    int got_subtitle = 0;
    int result = avcodec_decode_subtitle2(demux->subtitle_decoder, &decoded,
                                          &got_subtitle, packet);
    if (result < 0)
    {
        avsubtitle_free(&decoded);
        demux->failed = 1;
        set_av_error(error, error_size, "Cannot decode subtitle cue", result);
        return -1;
    }
    if (!got_subtitle)
    {
        avsubtitle_free(&decoded);
        return 0;
    }
    const char *text = NULL;
    if (decoded.num_rects == 1 && decoded.rects && decoded.rects[0])
        text = subtitle_rect_text(decoded.rects[0]);
    if (!text)
    {
        avsubtitle_free(&decoded);
        demux->failed = 1;
        set_error(error, error_size, "Unsupported mov_text subtitle cue");
        return -1;
    }

    AVRational time_base =
        demux->format->streams[demux->subtitle_track]->time_base;
    int64_t start_time_us;
    int64_t end_time_us;
    int timing_valid;
    if (decoded.end_display_time > decoded.start_display_time)
    {
        timing_valid =
            subtitle_time_us(&decoded, packet, time_base,
                             decoded.start_display_time, &start_time_us) &&
            subtitle_time_us(&decoded, packet, time_base,
                             decoded.end_display_time, &end_time_us);
    }
    else if (packet->duration > 0 &&
             subtitle_time_us(&decoded, packet, time_base, 0, &start_time_us))
    {
        int64_t duration_us = av_rescale_q(packet->duration, time_base,
                                           AV_TIME_BASE_Q);
        timing_valid = duration_us > 0 && start_time_us <= INT64_MAX - duration_us;
        if (timing_valid)
            end_time_us = start_time_us + duration_us;
    }
    else
    {
        timing_valid = 0;
    }
    if (!timing_valid || end_time_us <= start_time_us)
    {
        avsubtitle_free(&decoded);
        demux->failed = 1;
        set_error(error, error_size, "Subtitle cue has invalid timing");
        return -1;
    }
    size_t text_size;
    if (!copy_subtitle_text(demux, text, &text_size,
                            error, error_size))
    {
        avsubtitle_free(&decoded);
        demux->failed = 1;
        return -1;
    }
    subtitle->text = demux->subtitle_text;
    subtitle->text_size = text_size;
    subtitle->start_time_us = start_time_us;
    subtitle->end_time_us = end_time_us;
    avsubtitle_free(&decoded);
    return 1;
}

int video_demux_subtitle_next(VideoDemux *demux, VideoSubtitle *subtitle,
                              char *error, size_t error_size)
{
    if (!demux || !subtitle || !demux->subtitle_decoder)
    {
        set_error(error, error_size, "Subtitle decoder is not open");
        return -1;
    }
    if (demux->failed)
    {
        set_error(error, error_size, "Media source is in a failed state");
        return -1;
    }

    for (;;)
    {
        if (demux->demux_eof)
        {
            if (demux->subtitle_flushed)
            {
                set_error(error, error_size, "");
                return 0;
            }
            AVPacket flush_packet = {0};
            int result = decode_subtitle_packet(demux, &flush_packet, subtitle,
                                                error, error_size);
            if (result != 0)
                return result;
            demux->subtitle_flushed = 1;
            set_error(error, error_size, "");
            return 0;
        }

        av_packet_unref(demux->packet);
        int result = av_read_frame(demux->format, demux->packet);
        if (demux->io_error)
        {
            demux->failed = 1;
            set_error(error, error_size, "Cannot read media source");
            return -1;
        }
        if (result == AVERROR_EOF)
        {
            demux->demux_eof = 1;
            continue;
        }
        if (result < 0)
        {
            demux->failed = 1;
            set_av_error(error, error_size, "Cannot read subtitle packet", result);
            return -1;
        }
        if (demux->packet->size < 0 ||
            (size_t)demux->packet->size > VIDEO_MAX_PACKET_SIZE)
        {
            demux->failed = 1;
            set_error(error, error_size, "Media packet exceeds 16 MiB");
            return -1;
        }
        if (demux->packet->stream_index != demux->subtitle_track)
            continue;
        result = decode_subtitle_packet(demux, demux->packet, subtitle,
                                        error, error_size);
        if (result != 0)
            return result;
    }
}