#include "core/gl_desktop.h"
#include "audio/audio_wrapper.h"
#include "core/file_watcher.h"
#include "desktop/desktop_recording.h"
#include "file/file_wrapper.h"
#include "video/video_composition.h"
#include "video/video_demux.h"
#include "quickjs.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static void remember_window(void *context, Window *window, const SDL_Event *event)
{
    (void)window;
    if (event->type == SDL_WINDOWEVENT)
        *(SDL_Window **)context = SDL_GetWindowFromID(event->window.windowID);
}

static void draw(Window *window, bool shader)
{
    int width, height;
    window_get_size(window, &width, &height);
    window_begin_frame(window, window_get_time(window));
    if (shader)
    {
        assert(window_gl_bind_screen(window));
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        
        const char vertex[] =
            "#version 300 es\nin vec2 a_position; void main(){gl_Position=vec4(a_position,0,1);}";
        const char fragment[] =
            "#version 300 es\nprecision highp float;\nout vec4 color;\n"
            "void main(){color=vec4(0,1,0,1);}";
        int program = window_gl_create_program_from_source(
            window, vertex, strlen(vertex), fragment, strlen(fragment));
        if (program <= 0)
            fprintf(stderr, "%s\n", window_gl_get_error(window));
        assert(program > 0);
        assert(window_gl_draw_fullscreen(window, program));
    }
    else
    {
        SkiaCanvas *canvas = window_get_canvas(window);
        skia_canvas_clear(canvas, 0xff0000ff);
        SkiaPaint *paint = skia_paint_create();
        skia_paint_set_color(paint, 0xffff0000);
        skia_canvas_draw_rect(canvas, 0, 0, (float)width, (float)height / 2, paint);
        skia_paint_destroy(paint);
    }
    window_present(window);
}

static void check_pixel(const VideoFrame *frame, int x, int y, int r, int g, int b)
{
    const uint8_t *pixel = frame->rgba + (size_t)y * (size_t)frame->stride + (size_t)x * 4;
    assert(abs((int)pixel[0] - r) < 30);
    assert(abs((int)pixel[1] - g) < 30);
    assert(abs((int)pixel[2] - b) < 30);
}

static float *decode_audio(FileContext *files, const char *path, size_t *frames)
{
    char error[256];
    VideoDemux *demux = video_demux_open_file(files, path, error, sizeof(error));
    assert(demux && video_demux_track_count(demux) == 2);
    VideoTrack track;
    assert(video_demux_track(demux, 1, &track));
    assert(track.type == VIDEO_TRACK_AUDIO && strcmp(track.codec, "aac") == 0);
    assert(track.sample_rate == AUDIO_SAMPLE_RATE && track.channels == 2);
    assert(video_demux_audio_open(demux, 1, error, sizeof(error)));
    size_t capacity = AUDIO_SAMPLE_RATE * 10;
    float *samples = calloc(capacity * 2, sizeof(float));
    assert(samples);
    *frames = 0;
    VideoAudioBlock block;
    int status;
    while ((status = video_demux_audio_next(demux, &block, error, sizeof(error))) == 1)
    {
        int64_t first = llround((double)block.pts_us * AUDIO_SAMPLE_RATE / 1000000.0);
        size_t skip = first < 0 ? (size_t)-first : 0;
        if (skip >= block.frame_count)
            continue;
        if (first < 0)
            first = 0;
        size_t count = block.frame_count - skip;
        assert((size_t)first + count <= capacity);
        memcpy(samples + (size_t)first * 2, block.samples + skip * 2,
               count * 2 * sizeof(float));
        if (*frames < (size_t)first + count)
            *frames = (size_t)first + count;
    }
    if (status < 0)
        fprintf(stderr, "%s\n", error);
    assert(status == 0);
    video_demux_close(demux);
    return samples;
}

static double audio_rms(const float *samples, size_t frames, double start, double end,
                         int channel)
{
    size_t first = (size_t)(start * AUDIO_SAMPLE_RATE);
    size_t last = (size_t)(end * AUDIO_SAMPLE_RATE);
    assert(last <= frames && first < last);
    double energy = 0;
    for (size_t i = first; i < last; ++i)
        energy += (double)samples[i * 2 + channel] * samples[i * 2 + channel];
    return sqrt(energy / (double)(last - first));
}

static void check_recorded_sound(Window *window, const char *directory, FileContext *files)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/sound.mp4", directory);
    double origin = window_get_time(window);
    DesktopRecording *recording = desktop_recording_start(window, path);
    assert(recording);
    draw(window, false);
    SDL_Delay(160);
    AudioContext *audio = audio_create();
    assert(audio);
    int oscillator = audio_create_oscillator(audio);
    assert(oscillator >= 0);
    audio_oscillator_set_frequency(audio, oscillator, 440);
    audio_oscillator_set_gain(audio, oscillator, 0.4f);
    double tone = window_get_time(window) - origin;
    audio_oscillator_start(audio, oscillator);
    SDL_Delay(240);
    audio_pause(audio);
    double pause = window_get_time(window) - origin;
    SDL_Delay(200);
    double resume = window_get_time(window) - origin;
    audio_start(audio);
    SDL_Delay(240);
    audio_destroy(audio);
    SDL_Delay(200);

    audio = audio_create();
    assert(audio);
    int buffer = audio_create_buffer(audio, 22050, 2, 22050);
    assert(buffer >= 0);
    float *source = malloc(22050 * 2 * sizeof(float));
    assert(source);
    for (int i = 0; i < 22050; ++i)
    {
        source[i * 2] = 0.2f * sinf(6.283185307f * 440.0f * (float)i / 22050.0f);
        source[i * 2 + 1] = 0.4f * sinf(6.283185307f * 880.0f * (float)i / 22050.0f);
    }
    audio_buffer_set_data(audio, buffer, source, 0, 22050 * 2);
    double buffered = window_get_time(window) - origin;
    int playback = audio_play_buffer(audio, buffer, true, 1);
    assert(playback >= 0);
    SDL_Delay(240);
    audio_stop_buffer(audio, playback);
    audio_set_master_gain(audio, 0);
    int stream = audio_output_open(audio, 2, 500);
    assert(stream >= 0);
    assert(audio_output_write(audio, stream, source, 22050) > 10000);
    double streamed = window_get_time(window) - origin;
    SDL_Delay(240);
    audio_output_close(audio, stream);
    free(source);
    double stopped = window_get_time(window) - origin;
    SDL_Delay(200);
    double duration = window_get_time(window) - origin;
    assert(desktop_recording_finish(recording));
    audio_destroy(audio);

    size_t frames;
    float *samples = decode_audio(files, "assets/sound.mp4", &frames);
    assert(fabs((double)frames / AUDIO_SAMPLE_RATE - duration) < 0.08);
    assert(audio_rms(samples, frames, 0.04, 0.10, 0) < 0.003);
    double rms = audio_rms(samples, frames, tone + 0.07, tone + 0.14, 0);
    assert(rms > 0.11 && rms < 0.17);
    assert(audio_rms(samples, frames, pause + 0.08, pause + 0.15, 0) < 0.003);
    assert(audio_rms(samples, frames, resume + 0.07, resume + 0.14, 0) > 0.11);
    double left = audio_rms(samples, frames, buffered + 0.07, buffered + 0.14, 0);
    double right = audio_rms(samples, frames, buffered + 0.07, buffered + 0.14, 1);
    assert(left > 0.05 && left < 0.10 && right > left * 1.7);
    left = audio_rms(samples, frames, streamed + 0.07, streamed + 0.14, 0);
    right = audio_rms(samples, frames, streamed + 0.07, streamed + 0.14, 1);
    assert(left > 0.11 && left < 0.18 && right > left * 1.7);
    assert(audio_rms(samples, frames, stopped + 0.08, stopped + 0.15, 0) < 0.003);
    free(samples);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/sound.json", directory);
    assert(unlink(path) == 0);
}

int main(void)
{
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    char directory[] = "/tmp/budo-recording-XXXXXX";
    assert(mkdtemp(directory));
    char path[512];
    snprintf(path, sizeof(path), "%s/session.mp4", directory);
    WindowConfig config = {.title = "Recording test", .project_dir = ".",
                           .width = 96, .height = 64, .resizable = true};
    Window *window = window_create(&config);
    if (!window)
    {
        rmdir(directory);
        return 77;
    }
    InputState input;
    input_init(&input);
    SDL_Window *sdl_window = NULL;
    WindowObservers observers = {&sdl_window, remember_window, NULL};
    window_set_observers(window, &observers);
    window_poll_events(window, &input);
    assert(sdl_window);
    int width, height;
    window_get_size(window, &width, &height);
    
    width -= !(width & 1);
    height -= !(height & 1);
    window_resize(window, width, height);
    FileWatcher *watcher = file_watcher_create(directory, 0);
    assert(watcher);
    double started = window_get_time(window);
    DesktopRecording *recording = desktop_recording_start(window, path);
    assert(recording);
    draw(window, false);
    SDL_Delay(80);

    SDL_Event events[] = {
        {.motion = {.type = SDL_MOUSEMOTION, .x = 12, .y = 20}},
        {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x = 12, .y = 20}},
        {.button = {.type = SDL_MOUSEBUTTONUP, .button = SDL_BUTTON_LEFT, .x = 12, .y = 20}},
        {.wheel = {.type = SDL_MOUSEWHEEL, .y = -2}},
        {.key = {.type = SDL_KEYDOWN, .keysym = {.scancode = SDL_SCANCODE_A, .sym = SDLK_a}}},
        {.key = {.type = SDL_KEYDOWN, .repeat = 1, .keysym = {.scancode = SDL_SCANCODE_A, .sym = SDLK_a}}},
        {.key = {.type = SDL_KEYUP, .keysym = {.scancode = SDL_SCANCODE_A, .sym = SDLK_a}}},
        {.text = {.type = SDL_TEXTINPUT, .text = "\"\\\n"}},
    };
    for (size_t i = 0; i + 1 < sizeof(events) / sizeof(events[0]); ++i)
        assert(SDL_PushEvent(&events[i]) == 1);
    assert(window_poll_events(window, &input));
    
    desktop_recording_event(recording, window, &events[7]);

    SDL_SetWindowSize(sdl_window, 128, 32);
    SDL_Delay(30);
    assert(window_poll_events(window, &input));
    draw(window, false);
    SDL_Delay(80);
    SDL_SetWindowSize(sdl_window, 32, 128);
    SDL_Delay(30);
    assert(window_poll_events(window, &input));
    for (int i = 0; i < 5; ++i)
        draw(window, true);
    SDL_Delay(200);
    assert(!file_watcher_poll(watcher, 1));
    double elapsed = window_get_time(window) - started;
    assert(desktop_recording_finish(recording));
    assert(file_watcher_poll(watcher, 2));
    file_watcher_destroy(watcher);

    FileContext *files = file_create(directory);
    assert(files);
    char error[256];
    VideoDemux *demux = video_demux_open_file(files, "assets/session.mp4", error, sizeof(error));
    assert(demux);
    VideoTrack track;
    assert(video_demux_track(demux, 0, &track));
    assert(track.frame_rate_num == 30 * track.frame_rate_den);
    assert(track.sample_aspect_ratio_num == 1 && track.sample_aspect_ratio_den == 1);
    assert(video_demux_decode_open(demux, 0, error, sizeof(error)));
    VideoFrame frame;
    int frames = 0, previous_stage = 0, status;
    bool seen[3] = {false};
    while ((status = video_demux_decode_next(demux, &frame, error, sizeof(error))) == 1)
    {
        assert(frame.width == width && frame.height == height);
        assert(frame.pts * frame.time_base_num * 30 == (int64_t)frames * frame.time_base_den);
        const uint8_t *center = frame.rgba + (size_t)(height / 2) * frame.stride + (width / 2) * 4;
        const uint8_t *top = frame.rgba + (size_t)4 * frame.stride + (width / 2) * 4;
        int stage = center[1] > 200 ? 2 : top[0] < 30 ? 1 : 0;
        assert(stage >= previous_stage);
        previous_stage = stage;
        seen[stage] = true;
        if (stage == 0)
        {
            check_pixel(&frame, width / 2, height / 4, 255, 0, 0);
            check_pixel(&frame, width / 2, height * 3 / 4, 0, 0, 255);
        }
        else if (stage == 1)
        {
            check_pixel(&frame, width / 2, 4, 0, 0, 0);
            check_pixel(&frame, width / 2, height / 2 - 8, 255, 0, 0);
            check_pixel(&frame, width / 2, height / 2 + 8, 0, 0, 255);
        }
        else
        {
            check_pixel(&frame, 4, height / 2, 0, 0, 0);
            check_pixel(&frame, width / 2, height / 2, 0, 255, 0);
        }
        ++frames;
    }
    assert(status == 0 && seen[0] && seen[1] && seen[2]);
    double seconds = (double)frames / 30;
    assert(fabs(seconds - elapsed) < 0.05);
    video_demux_close(demux);
    VideoCompositionConfig composition_config = {
        .output_width = 64, .output_height = 64,
        .inset_x = 48, .inset_y = 48, .inset_width = 16, .inset_height = 16,
    };
    VideoComposition *composition = video_composition_open_files(
        files, "assets/session.mp4", "assets/session.mp4", &composition_config, error, sizeof(error));
    if (!composition)
        fprintf(stderr, "%s\n", error);
    assert(composition && video_composition_frame_count(composition) == frames);
    uint8_t composed[64 * 64 * 4];
    int composed_frames = 0;
    int64_t pts;
    while ((status = video_composition_next(composition, composed, sizeof(composed),
                                             &pts, error, sizeof(error))) == 1)
        ++composed_frames;
    assert(status == 0 && composed_frames == frames);
    video_composition_close(composition);
    size_t audio_frames;
    float *silent = decode_audio(files, "assets/session.mp4", &audio_frames);
    assert(fabs((double)audio_frames / AUDIO_SAMPLE_RATE - seconds) < 0.08);
    for (size_t i = 0; i < audio_frames * 2; ++i)
        assert(fabsf(silent[i]) < 0.0001f);
    free(silent);

    size_t json_size;
    char *json = file_read_text(files, "assets/session.json", &json_size);
    assert(json);
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = JS_NewContext(runtime);
    JSValue parsed = JS_ParseJSON(context, json, json_size, "events");
    assert(!JS_IsException(parsed));
    JSValue global = JS_GetGlobalObject(context);
    assert(JS_SetPropertyStr(context, global, "recording", parsed) == 1);
    JS_FreeValue(context, global);
    const char script[] =
        "const e = recording.events;"
        "if (recording.fit !== 'contain' || recording.version !== 1) throw Error('header');"
        "const expected = ['pointermove','pointerdown','pointerup','wheel','keydown','keydown','keyup','textinput'];"
        "const start = e.findIndex(x => x.type === 'pointerdown') - 1;"
        "if (JSON.stringify(e.slice(start,start+8).map(x=>x.type)) !== JSON.stringify(expected)) throw Error('order');"
        "if (e[start+1].buttons !== 1 || e[start+2].buttons !== 0) throw Error('buttons');"
        "if (!e[start+5].repeat || e[start+7].text !== '\\\"\\\\\\n') throw Error('data');"
        "if (e.filter(x=>x.type==='resize').length < 2) throw Error('resize');"
        "if (e.some((x,i)=>i && x.timestampUs < e[i-1].timestampUs)) throw Error('time');";
    JSValue result = JS_Eval(context, script, strlen(script), "checks", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(context);
        const char *message = JS_ToCString(context, exception);
        fprintf(stderr, "%s\n", message);
        JS_FreeCString(context, message);
        JS_FreeValue(context, exception);
    }
    assert(!JS_IsException(result));
    JS_FreeValue(context, result);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    free(json);
    check_recorded_sound(window, directory, files);
    file_destroy(files);
    window_destroy(window);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/session.json", directory);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}