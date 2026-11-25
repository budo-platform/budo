#include "video/video_composition.h"

#include "video/video_demux.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavutil/mathematics.h>

#define VIDEO_COMPOSITION_FRAME_RATE 30
#define VIDEO_COMPOSITION_MAX_PIXELS (16u * 1024u * 1024u)

typedef struct VideoCompositionInput
{
    VideoDemux *demux;
    VideoTrack track;
    int64_t expected_frames;
    int64_t decoded_frames;
    int64_t first_pts;
    int64_t previous_pts;
    int have_first_pts;
    VideoFrame pending_frame;
    int has_pending_frame;
} VideoCompositionInput;

struct VideoComposition
{
    VideoCompositionInput inputs[2];
    VideoCompositionConfig config;
    int64_t output_frame;
    int finished;
    int failed;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size)
        snprintf(error, error_size, "%s", message);
}

static int abs_difference_within_one(int64_t first, int64_t second)
{
    if (first >= second)
        return (uint64_t)first - (uint64_t)second <= 1;
    return (uint64_t)second - (uint64_t)first <= 1;
}

static int track_frame_count(const VideoTrack *track, int64_t *frame_count)
{
    AVRational time_base = {track->time_base_num, track->time_base_den};
    AVRational frame_duration = {1, VIDEO_COMPOSITION_FRAME_RATE};
    int64_t count = av_rescale_q_rnd(
        track->duration, time_base, frame_duration,
        AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    if (count <= 0)
        return 0;
    int64_t represented_duration =
        av_rescale_q(count, frame_duration, time_base);
    if (!abs_difference_within_one(track->duration, represented_duration))
        return 0;
    *frame_count = count;
    return 1;
}

static int validate_track(const VideoTrack *track, int64_t *frame_count,
                          char *error, size_t error_size)
{
    const uint32_t required_metadata =
        VIDEO_TRACK_HAS_START_TIME | VIDEO_TRACK_HAS_DURATION |
        VIDEO_TRACK_HAS_DIMENSIONS | VIDEO_TRACK_HAS_FRAME_RATE |
        VIDEO_TRACK_HAS_SAMPLE_ASPECT_RATIO | VIDEO_TRACK_HAS_ROTATION;
    if (track->type != VIDEO_TRACK_VIDEO ||
        (track->metadata & required_metadata) != required_metadata ||
        track->time_base_num <= 0 || track->time_base_den <= 0 ||
        track->duration <= 0 || track->width <= 0 || track->height <= 0)
    {
        set_error(error, error_size,
                  "Video input needs known dimensions, timing, and duration");
        return 0;
    }
    if (track->frame_rate_num <= 0 || track->frame_rate_den <= 0 ||
        av_cmp_q((AVRational){track->frame_rate_num, track->frame_rate_den},
                 (AVRational){VIDEO_COMPOSITION_FRAME_RATE, 1}) != 0)
    {
        set_error(error, error_size, "Video input must be nominal 30 fps");
        return 0;
    }
    if (track->sample_aspect_ratio_num <= 0 ||
        track->sample_aspect_ratio_den <= 0 ||
        av_cmp_q((AVRational){track->sample_aspect_ratio_num,
                             track->sample_aspect_ratio_den},
                 (AVRational){1, 1}) != 0 ||
        track->rotation_degrees != 0)
    {
        set_error(error, error_size,
                  "Video input must use square pixels and no display rotation");
        return 0;
    }
    if (track->color_primaries != VIDEO_COLOR_BT709 ||
        track->color_transfer != VIDEO_COLOR_BT709 ||
        track->color_matrix != VIDEO_COLOR_BT709 ||
        (track->color_range != VIDEO_COLOR_RANGE_FULL &&
         track->color_range != VIDEO_COLOR_RANGE_LIMITED))
    {
        set_error(error, error_size,
                  "Video input must use supported BT.709 SDR color metadata");
        return 0;
    }
    if (!track_frame_count(track, frame_count))
    {
        set_error(error, error_size,
                  "Video duration must represent a whole number of 30 fps frames");
        return 0;
    }
    return 1;
}

static int first_video_track(VideoDemux *demux, VideoTrack *track,
                             int *track_index, char *error, size_t error_size)
{
    for (int i = 0; i < video_demux_track_count(demux); ++i)
    {
        if (video_demux_track(demux, i, track) &&
            track->type == VIDEO_TRACK_VIDEO)
        {
            *track_index = i;
            return 1;
        }
    }
    set_error(error, error_size, "Video input has no video track");
    return 0;
}

static int validate_frame(const VideoCompositionInput *input,
                          const VideoFrame *frame, int64_t ordinal,
                          char *error, size_t error_size)
{
    const VideoTrack *track = &input->track;
    if (!frame->rgba || frame->pts == INT64_MIN ||
        frame->time_base_num != track->time_base_num ||
        frame->time_base_den != track->time_base_den ||
        frame->width != track->width || frame->height != track->height)
    {
        set_error(error, error_size,
                  "Decoded frame does not match the validated video track");
        return 0;
    }
    if (frame->interlaced)
    {
        set_error(error, error_size, "Interlaced video input is unsupported");
        return 0;
    }
    if (frame->sample_aspect_ratio_num <= 0 ||
        frame->sample_aspect_ratio_den <= 0 ||
        av_cmp_q((AVRational){frame->sample_aspect_ratio_num,
                             frame->sample_aspect_ratio_den},
                 (AVRational){1, 1}) != 0 ||
        frame->color_primaries != VIDEO_COLOR_BT709 ||
        frame->color_transfer != VIDEO_COLOR_BT709 ||
        frame->color_matrix != VIDEO_COLOR_BT709 ||
        (frame->color_range != VIDEO_COLOR_RANGE_FULL &&
         frame->color_range != VIDEO_COLOR_RANGE_LIMITED))
    {
        set_error(error, error_size,
                  "Decoded frame changed to unsupported geometry or color");
        return 0;
    }

    int64_t expected_offset = av_rescale_q(
        ordinal, (AVRational){1, VIDEO_COMPOSITION_FRAME_RATE},
        (AVRational){track->time_base_num, track->time_base_den});
    if (expected_offset == INT64_MIN || expected_offset == INT64_MAX ||
        input->first_pts > INT64_MAX - expected_offset)
    {
        set_error(error, error_size, "Video frame timestamp overflows");
        return 0;
    }
    int64_t expected_pts = input->first_pts + expected_offset;
    if (frame->pts <= input->previous_pts ||
        !abs_difference_within_one(frame->pts, expected_pts))
    {
        set_error(error, error_size,
                  "Video input timestamps are not ordered 30 fps CFR");
        return 0;
    }
    return 1;
}

VideoComposition *video_composition_open_files(
    FileContext *files, const char *background_path, const char *inset_path,
    const VideoCompositionConfig *config, char *error, size_t error_size)
{
    if (!files || !background_path || !inset_path || !config ||
        config->output_width <= 0 || config->output_height <= 0 ||
        config->output_width > INT_MAX / 4 ||
        (uint64_t)config->output_width * (uint64_t)config->output_height >
            VIDEO_COMPOSITION_MAX_PIXELS ||
        config->inset_x < 0 || config->inset_y < 0 ||
        config->inset_width <= 0 || config->inset_height <= 0 ||
        config->inset_x > config->output_width - config->inset_width ||
        config->inset_y > config->output_height - config->inset_height)
    {
        set_error(error, error_size, "Invalid video composition arguments");
        return NULL;
    }

    VideoComposition *composition = calloc(1, sizeof(*composition));
    if (!composition)
    {
        set_error(error, error_size, "Cannot allocate video composition");
        return NULL;
    }
    composition->config = *config;
    composition->inputs[0].first_pts = INT64_MIN;
    composition->inputs[0].previous_pts = INT64_MIN;
    composition->inputs[1].first_pts = INT64_MIN;
    composition->inputs[1].previous_pts = INT64_MIN;

    const char *paths[2] = {background_path, inset_path};
    for (int i = 0; i < 2; ++i)
    {
        VideoCompositionInput *input = &composition->inputs[i];
        input->demux = video_demux_open_file(files, paths[i], error, error_size);
        if (!input->demux)
            goto fail;
        int track_index;
        if (!first_video_track(input->demux, &input->track, &track_index,
                               error, error_size) ||
            !validate_track(&input->track, &input->expected_frames,
                            error, error_size) ||
            !video_demux_decode_open(input->demux, track_index,
                                     error, error_size))
            goto fail;
    }
    for (int i = 0; i < 2; ++i)
    {
        VideoCompositionInput *input = &composition->inputs[i];
        int status = video_demux_decode_next(input->demux,
                                             &input->pending_frame,
                                             error, error_size);
        if (status != 1)
        {
            if (status == 0)
                set_error(error, error_size,
                          "Video input has no decodable first frame");
            goto fail;
        }
        input->first_pts = input->pending_frame.pts;
        input->have_first_pts = 1;
        if (!validate_frame(input, &input->pending_frame, 0,
                            error, error_size))
            goto fail;
        input->has_pending_frame = 1;
    }
    set_error(error, error_size, "");
    return composition;

fail:
    video_composition_close(composition);
    return NULL;
}

int64_t video_composition_frame_count(const VideoComposition *composition)
{
    if (!composition)
        return 0;
    return composition->inputs[0].expected_frames <
                   composition->inputs[1].expected_frames
               ? composition->inputs[0].expected_frames
               : composition->inputs[1].expected_frames;
}

int video_composition_seek_frame(VideoComposition *composition,
                                 int64_t ordinal,
                                 char *error, size_t error_size)
{
    if (!composition || ordinal < 0)
    {
        set_error(error, error_size, "Invalid video composition seek");
        return -1;
    }
    if (composition->failed)
    {
        set_error(error, error_size, "Video composition is in a failed state");
        return -1;
    }

    int64_t frame_count = video_composition_frame_count(composition);
    if (ordinal >= frame_count)
    {
        for (int i = 0; i < 2; ++i)
            composition->inputs[i].has_pending_frame = 0;
        composition->output_frame = frame_count;
        composition->finished = 1;
        set_error(error, error_size, "");
        return 0;
    }

    for (int i = 0; i < 2; ++i)
    {
        VideoCompositionInput *input = &composition->inputs[i];
        int64_t offset = av_rescale_q(
            ordinal, (AVRational){1, VIDEO_COMPOSITION_FRAME_RATE},
            (AVRational){input->track.time_base_num,
                         input->track.time_base_den});
        if (offset == INT64_MIN || offset == INT64_MAX ||
            input->first_pts > INT64_MAX - offset)
        {
            set_error(error, error_size, "Video seek timestamp overflows");
            composition->failed = 1;
            return -1;
        }
        int64_t timestamp = input->first_pts + offset;
        int status = video_demux_seek_time(
            input->demux, timestamp, &input->pending_frame,
            error, error_size);
        if (status != 1)
        {
            if (status == 0)
                set_error(error, error_size,
                          "Video seek reached EOF before the requested frame");
            composition->failed = 1;
            return -1;
        }
        VideoCompositionInput validation = *input;
        validation.previous_pts = INT64_MIN;
        if (!validate_frame(&validation, &input->pending_frame, ordinal,
                            error, error_size))
        {
            composition->failed = 1;
            return -1;
        }
        input->decoded_frames = ordinal;
        input->previous_pts = INT64_MIN;
        input->has_pending_frame = 1;
    }
    composition->output_frame = ordinal;
    composition->finished = 0;
    set_error(error, error_size, "");
    return 1;
}

int video_composition_next(VideoComposition *composition, uint8_t *output,
                           size_t output_size, int64_t *pts_us,
                           char *error, size_t error_size)
{
    if (!composition || !output || !pts_us)
    {
        set_error(error, error_size, "Invalid video composition request");
        return -1;
    }
    if (composition->failed)
    {
        set_error(error, error_size, "Video composition is in a failed state");
        return -1;
    }
    if (composition->finished)
    {
        set_error(error, error_size, "");
        return 0;
    }

    VideoFrame frames[2];
    int statuses[2];
    for (int i = 0; i < 2; ++i)
    {
        VideoCompositionInput *input = &composition->inputs[i];
        if (input->has_pending_frame)
        {
            frames[i] = input->pending_frame;
            input->has_pending_frame = 0;
            statuses[i] = 1;
        }
        else
            statuses[i] = video_demux_decode_next(input->demux, &frames[i],
                                                   error, error_size);
        if (statuses[i] < 0)
            goto fail;
        if (statuses[i] == 0)
        {
            if (input->decoded_frames != input->expected_frames)
            {
                set_error(error, error_size,
                          "Decoded frame count does not match known video duration");
                goto fail;
            }
        }
        else
        {
            if (input->decoded_frames >= input->expected_frames)
            {
                set_error(error, error_size,
                          "Video contains frames beyond its declared duration");
                goto fail;
            }
            if (!input->have_first_pts)
            {
                input->first_pts = frames[i].pts;
                input->have_first_pts = 1;
            }
            if (!validate_frame(input, &frames[i], input->decoded_frames,
                                error, error_size))
                goto fail;
            input->previous_pts = frames[i].pts;
            input->decoded_frames++;
        }
    }

    if (statuses[0] == 0 || statuses[1] == 0)
    {
        composition->finished = 1;
        set_error(error, error_size, "");
        return 0;
    }

    *pts_us = av_rescale_q(composition->output_frame,
                           (AVRational){1, VIDEO_COMPOSITION_FRAME_RATE},
                           (AVRational){1, 1000000});
    if (*pts_us == INT64_MIN || *pts_us == INT64_MAX ||
        composition->output_frame == INT64_MAX)
    {
        set_error(error, error_size, "Video composition timeline overflows");
        goto fail;
    }
    if (!video_compose_rgba(&frames[0], &frames[1], &composition->config,
                            output, output_size, error, error_size))
        goto fail;
    composition->output_frame++;
    set_error(error, error_size, "");
    return 1;

fail:
    composition->failed = 1;
    return -1;
}

int video_composition_export_file_controlled(
    FileContext *files, const char *background_path, const char *inset_path,
    const char *output_path, const VideoCompositionConfig *config,
    int video_bit_rate, const VideoCompositionExportControl *control,
    char *error, size_t error_size)
{
    if (!files || !background_path || !inset_path || !output_path ||
        !config || video_bit_rate <= 0)
    {
        set_error(error, error_size, "Invalid video export arguments");
        return 0;
    }

    VideoComposition *composition = video_composition_open_files(
        files, background_path, inset_path, config, error, error_size);
    if (!composition)
        return 0;

    size_t output_size = (size_t)config->output_width *
                         (size_t)config->output_height * 4u;
    uint8_t *output = malloc(output_size);
    if (!output)
    {
        set_error(error, error_size, "Cannot allocate video export frame");
        video_composition_close(composition);
        return 0;
    }

    VideoWriterConfig writer_config = {
        .width = config->output_width,
        .height = config->output_height,
        .frame_rate_num = VIDEO_COMPOSITION_FRAME_RATE,
        .frame_rate_den = 1,
        .video_bit_rate = video_bit_rate,
    };
    VideoWriter *writer = video_writer_open_file(
        files, output_path, &writer_config, error, error_size);
    if (!writer)
    {
        free(output);
        video_composition_close(composition);
        return 0;
    }

    int success = 0;
    int64_t completed_frames = 0;
    for (;;)
    {
        if (control && control->is_cancelled &&
            control->is_cancelled(control->context))
        {
            set_error(error, error_size, "Video export was cancelled");
            break;
        }
        int64_t pts_us;
        int status = video_composition_next(composition, output, output_size,
                                            &pts_us, error, error_size);
        if (status < 0)
            break;
        if (status == 0)
        {
            if (control && control->begin_finalization &&
                !control->begin_finalization(control->context))
            {
                set_error(error, error_size, "Video export was cancelled");
                break;
            }
            success = 1;
            break;
        }
        if (control && control->is_cancelled &&
            control->is_cancelled(control->context))
        {
            set_error(error, error_size, "Video export was cancelled");
            break;
        }
        if (!video_writer_write_frame(writer, output, output_size, pts_us,
                                      error, error_size))
            break;
        completed_frames++;
        if (control && control->progress)
            control->progress(control->context, completed_frames);
    }
    if (success)
    {
        success = video_writer_finish(writer, error, error_size);
        writer = NULL;
    }
    if (writer)
        video_writer_close(writer);
    free(output);
    video_composition_close(composition);
    return success;
}

int video_composition_export_file(
    FileContext *files, const char *background_path, const char *inset_path,
    const char *output_path, const VideoCompositionConfig *config,
    int video_bit_rate, char *error, size_t error_size)
{
    return video_composition_export_file_controlled(
        files, background_path, inset_path, output_path, config, video_bit_rate,
        NULL, error, error_size);
}

void video_composition_close(VideoComposition *composition)
{
    if (!composition)
        return;
    for (int i = 0; i < 2; ++i)
        video_demux_close(composition->inputs[i].demux);
    free(composition);
}