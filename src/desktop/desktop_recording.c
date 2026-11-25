#include "desktop/desktop_recording.h"

#include "audio/audio_ring.h"
#include "audio/audio_wrapper.h"
#include "core/platform_thread.h"
#include "file/file_wrapper.h"
#include "video/video_demux.h"

#include <SDL2/SDL.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORDING_MAX_PIXELS (16u * 1024u * 1024u)
#define RECORDING_FRAME_RATE 30
#define RECORDING_AUDIO_BLOCK_FRAMES 1024
#define RECORDING_AUDIO_BLOCKS 256
#define RECORDING_AUDIO_DELAY_US 100000

typedef struct RecordingAudioBlock
{
    int frames;
    int64_t first_frame;
    float samples[RECORDING_AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS];
} RecordingAudioBlock;

struct DesktopRecording
{
    Window *window;
    FileContext *files;
    FileContext *event_files;
    FileAtomicWriter *events;
    VideoWriter *writer;
    BudoThread thread;
    BudoMutex mutex;
    BudoCondition condition;
    SDL_sem *ready;
    RecordingAudioBlock audio[RECORDING_AUDIO_BLOCKS];
    volatile uint64_t audio_written;
    volatile uint64_t audio_read;
    SDL_atomic_t audio_error;
    int64_t audio_cursor;
    bool audio_attached;
    uint8_t *pending;
    int pending_width;
    int pending_height;
    int64_t pending_pts;
    int width;
    int height;
    double origin;
    int64_t last_capture;
    int64_t stop_time;
    bool stopping;
    bool first_event;
    bool failed;
    bool reported;
    uint32_t buttons;
    char error[256];
};

static void recording_fail(DesktopRecording *recording, const char *message)
{
    budo_mutex_lock(&recording->mutex);
    if (!recording->failed)
        snprintf(recording->error, sizeof(recording->error), "%s", message);
    recording->failed = true;
    budo_condition_broadcast(&recording->condition);
    budo_mutex_unlock(&recording->mutex);
}

bool desktop_recording_ok(DesktopRecording *recording)
{
    if (!recording)
        return true;
    budo_mutex_lock(&recording->mutex);
    bool ok = !recording->failed;
    if (!ok && !recording->reported)
    {
        fprintf(stderr, "Error: Recording failed: %s\n", recording->error);
        recording->reported = true;
    }
    budo_mutex_unlock(&recording->mutex);
    return ok;
}

static int64_t recording_time(DesktopRecording *recording)
{
    double now = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    return (int64_t)((now - recording->origin) * 1000000.0);
}

static void recording_audio(void *opaque, const float *samples, int frames,
                             int sample_rate, int channels, double timestamp)
{
    DesktopRecording *recording = opaque;
    if (sample_rate != AUDIO_SAMPLE_RATE || channels != AUDIO_CHANNELS)
    {
        SDL_AtomicSet(&recording->audio_error, 2);
        SDL_SemPost(recording->ready);
        return;
    }
    int64_t first_frame = llround((timestamp - recording->origin) * AUDIO_SAMPLE_RATE);
    for (int done = 0; done < frames;)
    {
        uint64_t written = audio_ring_load(&recording->audio_written);
        uint64_t read = audio_ring_load(&recording->audio_read);
        if (written - read == RECORDING_AUDIO_BLOCKS)
        {
            SDL_AtomicSet(&recording->audio_error, 1);
            SDL_SemPost(recording->ready);
            return;
        }
        RecordingAudioBlock *block = &recording->audio[written % RECORDING_AUDIO_BLOCKS];
        block->frames = frames - done;
        if (block->frames > RECORDING_AUDIO_BLOCK_FRAMES)
            block->frames = RECORDING_AUDIO_BLOCK_FRAMES;
        block->first_frame = first_frame + done;
        memcpy(block->samples, samples + (size_t)done * AUDIO_CHANNELS,
               (size_t)block->frames * AUDIO_CHANNELS * sizeof(float));
        done += block->frames;
        audio_ring_store(&recording->audio_written, written + 1);
    }
    SDL_SemPost(recording->ready);
}

static bool recording_write_audio(DesktopRecording *recording, const float *samples,
                                   int frames)
{
    char error[256];
    int64_t pts = av_rescale_q(recording->audio_cursor,
                              (AVRational){1, AUDIO_SAMPLE_RATE}, AV_TIME_BASE_Q);
    if (!video_writer_write_audio(recording->writer, samples, (size_t)frames,
                                   pts, error, sizeof(error)))
    {
        recording_fail(recording, error);
        return false;
    }
    recording->audio_cursor += frames;
    return true;
}

static bool recording_silence_until(DesktopRecording *recording, int64_t frame)
{
    const float silence[RECORDING_AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS] = {0};
    while (recording->audio_cursor < frame)
    {
        int64_t remaining = frame - recording->audio_cursor;
        int count = remaining < RECORDING_AUDIO_BLOCK_FRAMES
                        ? (int)remaining : RECORDING_AUDIO_BLOCK_FRAMES;
        if (!recording_write_audio(recording, silence, count))
            return false;
    }
    return true;
}

static bool recording_audio_until(DesktopRecording *recording, int64_t time_us,
                                   bool finishing)
{
    int64_t target = av_rescale_q(time_us > 0 ? time_us : 0, AV_TIME_BASE_Q,
                                  (AVRational){1, AUDIO_SAMPLE_RATE});
    uint64_t read = audio_ring_load(&recording->audio_read);
    while (read < audio_ring_load(&recording->audio_written))
    {
        RecordingAudioBlock *block = &recording->audio[read % RECORDING_AUDIO_BLOCKS];
        int64_t end = block->first_frame + block->frames;
        if (block->first_frame >= target || (!finishing && end > target))
            break;
        if (!recording_silence_until(recording, block->first_frame))
            return false;
        if (end > target)
            end = target;
        if (end > recording->audio_cursor)
        {
            int offset = (int)(recording->audio_cursor - block->first_frame);
            int frames = (int)(end - recording->audio_cursor);
            if (!recording_write_audio(recording,
                                        block->samples + (size_t)offset * AUDIO_CHANNELS,
                                        frames))
                return false;
        }
        audio_ring_store(&recording->audio_read, ++read);
    }
    if (read < audio_ring_load(&recording->audio_written))
    {
        int64_t next = recording->audio[read % RECORDING_AUDIO_BLOCKS].first_frame;
        if (target > next)
            target = next;
    }
    return recording_silence_until(recording, target);
}

static void write_events(DesktopRecording *recording, const char *text)
{
    if (!file_atomic_writer_write(recording->events, (const uint8_t *)text, strlen(text)))
        recording_fail(recording, "Cannot write input events");
}

static void contain_size(int source_width, int source_height, int width, int height,
                         int *scaled_width, int *scaled_height)
{
    if ((int64_t)source_width * height > (int64_t)source_height * width)
    {
        *scaled_width = width;
        *scaled_height = (int)((int64_t)source_height * width / source_width);
    }
    else
    {
        *scaled_height = height;
        *scaled_width = (int)((int64_t)source_width * height / source_height);
    }
    if (*scaled_width < 1)
        *scaled_width = 1;
    if (*scaled_height < 1)
        *scaled_height = 1;
}

static bool recording_video_until(DesktopRecording *recording, const uint8_t *pixels,
                                   size_t size, int64_t *frames, int64_t time_us)
{
    AVRational frame_time = {1, RECORDING_FRAME_RATE};
    int64_t target = av_rescale_q_rnd(time_us, AV_TIME_BASE_Q, frame_time, AV_ROUND_UP);
    char error[256];
    while (*frames < target)
    {
        int64_t pts = av_rescale_q(*frames, frame_time, AV_TIME_BASE_Q);
        if (!video_writer_write_frame(recording->writer, pixels, size, pts, error, sizeof(error)))
        {
            recording_fail(recording, error);
            return false;
        }
        ++*frames;
    }
    return true;
}

static BUDO_THREAD_RETURN recording_worker(void *opaque)
{
    DesktopRecording *recording = opaque;
    size_t size = (size_t)recording->width * (size_t)recording->height * 4;
    uint8_t *output = malloc(size);
    struct SwsContext *scaler = NULL;
    int64_t frames = 0;
    bool have_image = false;
    char error[256];
    if (!output)
        recording_fail(recording, "Out of memory allocating recording pixels");

    for (;;)
    {
        if (SDL_SemWaitTimeout(recording->ready, 10) < 0)
        {
            recording_fail(recording, "Cannot wait for recorded media");
            break;
        }
        budo_mutex_lock(&recording->mutex);
        bool stopping = recording->stopping;
        bool failed = recording->failed;
        int64_t time_us = stopping ? recording->stop_time : recording_time(recording);
        budo_mutex_unlock(&recording->mutex);
        if (failed)
            break;
        int audio_error = SDL_AtomicGet(&recording->audio_error);
        if (audio_error)
        {
            recording_fail(recording, audio_error == 1
                                          ? "Recording audio queue overflowed; encoding cannot keep up"
                                          : "Unsupported app audio output format");
            break;
        }
        
        if (!recording_audio_until(recording,
                                    stopping ? time_us : time_us - RECORDING_AUDIO_DELAY_US,
                                    stopping))
            break;

        budo_mutex_lock(&recording->mutex);
        if (!recording->pending)
        {
            budo_mutex_unlock(&recording->mutex);
            if (have_image && !recording_video_until(recording, output, size, &frames, time_us))
                break;
            if (stopping)
                break;
            continue;
        }
        uint8_t *source = recording->pending;
        int width = recording->pending_width;
        int height = recording->pending_height;
        int64_t pts = recording->pending_pts;
        recording->pending = NULL;
        budo_condition_broadcast(&recording->condition);
        budo_mutex_unlock(&recording->mutex);

        if (have_image && !recording_video_until(recording, output, size, &frames, pts))
        {
            free(source);
            break;
        }
        int scaled_width, scaled_height;
        contain_size(width, height, recording->width, recording->height,
                     &scaled_width, &scaled_height);
        int x = (recording->width - scaled_width) / 2;
        int y = (recording->height - scaled_height) / 2;
        memset(output, 0, size);
        scaler = sws_getCachedContext(scaler, width, height, AV_PIX_FMT_RGBA,
                                      scaled_width, scaled_height, AV_PIX_FMT_RGBA,
                                      SWS_BILINEAR, NULL, NULL, NULL);
        const uint8_t *sources[] = {source + (size_t)(height - 1) * (size_t)width * 4, NULL, NULL, NULL};
        int source_strides[] = {-width * 4, 0, 0, 0};
        uint8_t *destinations[] = {output + ((size_t)y * (size_t)recording->width + (size_t)x) * 4, NULL, NULL, NULL};
        int destination_strides[] = {recording->width * 4, 0, 0, 0};
        bool converted = scaler &&
                         sws_scale(scaler, sources, source_strides, 0, height,
                                   destinations, destination_strides) == scaled_height;
        free(source);
        if (!converted)
        {
            recording_fail(recording, "Cannot scale recording frame");
            break;
        }
        have_image = true;
        if (!recording_video_until(recording, output, size, &frames, time_us))
            break;
    }

    if (desktop_recording_ok(recording))
    {
        if (!video_writer_finish(recording->writer, error, sizeof(error)))
            recording_fail(recording, error);
    }
    else
        video_writer_close(recording->writer);
    recording->writer = NULL;
    sws_freeContext(scaler);
    free(output);
    return BUDO_THREAD_RESULT;
}

static void recording_frame(void *opaque, Window *window)
{
    DesktopRecording *recording = opaque;
    int64_t pts = recording_time(recording);
    if (!desktop_recording_ok(recording))
        return;
    int width, height;
    window_get_size(window, &width, &height);
    if (width <= 0 || height <= 0 ||
        (uint64_t)width * (uint64_t)height > RECORDING_MAX_PIXELS)
    {
        recording_fail(recording, "Recording source exceeds the 16-megapixel limit");
        return;
    }
    size_t size = (size_t)width * (size_t)height * 4;
    uint8_t *pixels = malloc(size);
    if (!pixels || !window_read_screen(window, pixels, size))
    {
        free(pixels);
        recording_fail(recording, "Cannot read the app screen");
        return;
    }
    budo_mutex_lock(&recording->mutex);
    while (recording->pending && !recording->failed)
        budo_condition_wait(&recording->condition, &recording->mutex);
    if (recording->failed)
        free(pixels);
    else
    {
        recording->pending = pixels;
        recording->pending_width = width;
        recording->pending_height = height;
        recording->pending_pts = recording->last_capture < 0 ? 0 : pts;
        recording->last_capture = pts;
        SDL_SemPost(recording->ready);
    }
    budo_mutex_unlock(&recording->mutex);
}

void desktop_recording_event(void *opaque, Window *window, const SDL_Event *event)
{
    DesktopRecording *recording = opaque;
    char details[512];
    const char *type;
    switch (event->type)
    {
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    {
        type = event->type == SDL_MOUSEMOTION ? "pointermove" :
               event->type == SDL_MOUSEBUTTONDOWN ? "pointerdown" : "pointerup";
        int x = event->type == SDL_MOUSEMOTION ? event->motion.x : event->button.x;
        int y = event->type == SDL_MOUSEMOTION ? event->motion.y : event->button.y;
        if (event->type == SDL_MOUSEMOTION)
            recording->buttons = event->motion.state;
        else if (event->button.button > 0 && event->button.button <= 32)
        {
            uint32_t mask = UINT32_C(1) << (event->button.button - 1);
            if (event->type == SDL_MOUSEBUTTONDOWN)
                recording->buttons |= mask;
            else
                recording->buttons &= ~mask;
        }
        snprintf(details, sizeof(details), ",\"x\":%d,\"y\":%d,\"button\":%u,\"buttons\":%u",
                 (int)(x * window_get_dpi_scale(window)),
                 (int)(y * window_get_dpi_scale(window)),
                 event->type == SDL_MOUSEMOTION ? 0 : event->button.button,
                 recording->buttons);
        break;
    }
    case SDL_MOUSEWHEEL:
        type = "wheel";
        snprintf(details, sizeof(details), ",\"deltaX\":%d,\"deltaY\":%d,\"flipped\":%s",
                 event->wheel.x, event->wheel.y,
                 event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? "true" : "false");
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        type = event->type == SDL_KEYDOWN ? "keydown" : "keyup";
        snprintf(details, sizeof(details), ",\"scancode\":%d,\"keycode\":%d,\"modifiers\":%u,\"repeat\":%s",
                 event->key.keysym.scancode, event->key.keysym.sym,
                 event->key.keysym.mod, event->key.repeat ? "true" : "false");
        break;
    case SDL_TEXTINPUT:
    {
        type = "textinput";
        char escaped[sizeof(event->text.text) * 6 + 1];
        char *out = escaped;
        for (const unsigned char *p = (const unsigned char *)event->text.text; *p; ++p)
        {
            if (*p < 32 || *p == '"' || *p == '\\')
                out += snprintf(out, 7, "\\u%04x", *p);
            else
                *out++ = (char)*p;
        }
        *out = '\0';
        snprintf(details, sizeof(details), ",\"text\":\"%s\"", escaped);
        break;
    }
    case SDL_WINDOWEVENT:
        if (event->window.event != SDL_WINDOWEVENT_SIZE_CHANGED &&
            event->window.event != SDL_WINDOWEVENT_RESIZED &&
            event->window.event != SDL_WINDOWEVENT_FOCUS_LOST)
            return;
        type = event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ? "blur" : "resize";
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            recording->buttons = 0;
        details[0] = '\0';
        break;
    default:
        return;
    }
    if (!desktop_recording_ok(recording))
        return;
    int width, height;
    window_get_size(window, &width, &height);
    char text[768];
    snprintf(text, sizeof(text),
             "%s{\"timestampUs\":%lld,\"type\":\"%s\",\"windowWidth\":%d,\"windowHeight\":%d%s}",
             recording->first_event ? "" : ",\n",
             (long long)recording_time(recording), type, width, height, details);
    write_events(recording, text);
    recording->first_event = false;
}

DesktopRecording *desktop_recording_start(Window *window, const char *path)
{
    DesktopRecording *recording = calloc(1, sizeof(*recording));
    if (!recording)
    {
        fprintf(stderr, "Error: Cannot allocate desktop recording\n");
        return NULL;
    }
    recording->window = window;
    recording->first_event = true;
    recording->last_capture = -1;
    recording->origin = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    window_get_size(window, &recording->width, &recording->height);
    if (!budo_mutex_init(&recording->mutex))
    {
        fprintf(stderr, "Error: Cannot initialize recording mutex\n");
        free(recording);
        return NULL;
    }
    if (!budo_condition_init(&recording->condition))
    {
        fprintf(stderr, "Error: Cannot initialize recording condition\n");
        budo_mutex_destroy(&recording->mutex);
        free(recording);
        return NULL;
    }
    recording->ready = SDL_CreateSemaphore(0);
    if (!recording->ready)
    {
        recording_fail(recording, "Cannot initialize recording wake semaphore");
        goto failed;
    }
    char parent[PATH_MAX], root[PATH_MAX], video_path[FILE_MAX_PATH], json_path[FILE_MAX_PATH];
    size_t length = strlen(path);
    if (length <= 4 || length >= sizeof(parent) || strcmp(path + length - 4, ".mp4") != 0)
    {
        recording_fail(recording, "Output path must end in .mp4 and fit within PATH_MAX");
        goto failed;
    }
    memcpy(parent, path, length + 1);
    char *slash = strrchr(parent, '/');
    const char *name = slash ? slash + 1 : path;
    if (slash)
        *slash = '\0';
    if (!realpath(slash ? (parent[0] ? parent : "/") : ".", root))
    {
        recording_fail(recording, "Cannot resolve recording output directory");
        goto failed;
    }
    if (snprintf(video_path, sizeof(video_path), "files/%s", name) >= (int)sizeof(video_path) - 1)
    {
        recording_fail(recording, "Recording filename is too long");
        goto failed;
    }
    snprintf(json_path, sizeof(json_path), "%s", video_path);
    memcpy(json_path + strlen(json_path) - 4, ".json", 6);
    recording->files = file_create(root);
    if (!recording->files)
    {
        recording_fail(recording, "Cannot create recording file context");
        goto failed;
    }
    file_set_write_root(recording->files, root);
    recording->event_files = file_clone(recording->files);
    if (!recording->event_files)
    {
        recording_fail(recording, "Cannot create input event file context");
        goto failed;
    }
    VideoWriterConfig config = {
        .width = recording->width,
        .height = recording->height,
        .frame_rate_num = RECORDING_FRAME_RATE,
        .frame_rate_den = 1,
        .video_bit_rate = 12000000,
        .audio_sample_rate = AUDIO_SAMPLE_RATE,
        .audio_channels = AUDIO_CHANNELS,
        .audio_bit_rate = 192000,
    };
    char error[256];
    recording->writer = video_writer_open_file(recording->files, video_path, &config, error, sizeof(error));
    if (!recording->writer)
    {
        recording_fail(recording, error);
        goto failed;
    }
    recording->events = file_atomic_writer_open(recording->event_files, json_path);
    if (!recording->events)
    {
        recording_fail(recording, file_get_error(recording->event_files));
        goto failed;
    }
    char header[256];
    snprintf(header, sizeof(header),
             "{\"version\":1,\"width\":%d,\"height\":%d,\"fit\":\"contain\","
             "\"timestampUnit\":\"microseconds\",\"events\":[\n",
             recording->width, recording->height);
    write_events(recording, header);
    if (!desktop_recording_ok(recording))
        goto failed;
    if (!audio_set_output_observer(recording_audio, recording))
    {
        recording_fail(recording, "An app audio output observer is already attached");
        goto failed;
    }
    recording->audio_attached = true;
    if (!budo_thread_create(&recording->thread, recording_worker, recording))
    {
        recording_fail(recording, "Cannot start recording worker");
        goto failed;
    }
    WindowObservers observers = {recording, desktop_recording_event, recording_frame};
    window_set_observers(window, &observers);
    return recording;

failed:
    if (recording->audio_attached)
        audio_set_output_observer(NULL, NULL);
    desktop_recording_ok(recording);
    video_writer_close(recording->writer);
    if (recording->events)
        file_atomic_writer_finish(recording->events, false);
    file_destroy(recording->files);
    file_destroy(recording->event_files);
    budo_condition_destroy(&recording->condition);
    budo_mutex_destroy(&recording->mutex);
    if (recording->ready)
        SDL_DestroySemaphore(recording->ready);
    free(recording);
    return NULL;
}

bool desktop_recording_finish(DesktopRecording *recording)
{
    if (!recording)
        return true;
    audio_set_output_observer(NULL, NULL);
    window_set_observers(recording->window, NULL);
    budo_mutex_lock(&recording->mutex);
    recording->stop_time = recording_time(recording);
    recording->stopping = true;
    SDL_SemPost(recording->ready);
    budo_mutex_unlock(&recording->mutex);
    budo_thread_join(recording->thread);
    if (desktop_recording_ok(recording))
        write_events(recording, "\n]}\n");
    if (!file_atomic_writer_finish(recording->events, desktop_recording_ok(recording)))
        recording_fail(recording, "Cannot publish input events");
    bool ok = desktop_recording_ok(recording);
    free(recording->pending);
    file_destroy(recording->files);
    file_destroy(recording->event_files);
    budo_condition_destroy(&recording->condition);
    budo_mutex_destroy(&recording->mutex);
    SDL_DestroySemaphore(recording->ready);
    free(recording);
    return ok;
}