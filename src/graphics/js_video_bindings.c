#include "graphics/js_video_bindings.h"

#include "core/window.h"
#include "file/file_wrapper.h"
#include "graphics/js_tools.h"
#include "video/video_export.h"
#include "video/video_frame_writer.h"
#include "video/video_player.h"
#include "video/video_reader.h"
#include "video/video_texture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_OUTPUT_WIDTH 640
#define VIDEO_OUTPUT_HEIGHT 360
#define VIDEO_INSET_X 464
#define VIDEO_INSET_Y 254
#define VIDEO_INSET_WIDTH 160
#define VIDEO_INSET_HEIGHT 90
#define VIDEO_FRAME_RATE 30
#define VIDEO_BIT_RATE 2000000
#define VIDEO_READER_LIMIT 8
#define VIDEO_RETAINED_FRAME_LIMIT 8
#define VIDEO_RETAINED_BYTES_LIMIT (64u * 1024u * 1024u)

typedef struct
{
    uint32_t id;
    VideoPlayer *player;
    JSValue callbacks;
    bool closing;
    bool seeking;
    bool paused;
    bool ready;
    bool terminal;
    bool context_error_pending;
    bool context_invalid;
    int64_t duration_us;
    int64_t seek_us;
    char background_path[FILE_MAX_PATH];
    char inset_path[FILE_MAX_PATH];
} JsVideoPreview;

typedef struct
{
    uint32_t id;
    VideoExport *export;
    JSValue callbacks;
    char output_path[FILE_MAX_PATH];
    int64_t progress;
} JsVideoExport;

typedef struct
{
    uint32_t id;
    VideoFrameWriter *worker;
    JSValue callbacks;
    char output_path[FILE_MAX_PATH];
    bool cancelling;
} JsVideoFrameWriter;

typedef struct
{
    uint32_t id;
    VideoReader *reader;
    JSValue callbacks;
    JSValue request_callbacks;
    VideoTrack track;
    bool ready;
    bool closing;
    bool request_pending;
    bool open_cancel_pending;
    bool frame_cancel_pending;
    bool request_context_lost;
    bool frame_cancel_requested;
    size_t reserved_bytes;
} JsVideoReader;

typedef struct
{
    uint32_t id;
    VideoTexture texture;
    size_t bytes;
} JsVideoFrame;

struct JsVideoBindings
{
    JSContext *ctx;
    uint32_t next_id;
    char error[256];
    JsVideoPreview preview;
    JsVideoExport export;
    JsVideoFrameWriter writer;
    JsVideoReader readers[VIDEO_READER_LIMIT];
    JsVideoFrame frames[VIDEO_RETAINED_FRAME_LIMIT];
    size_t retained_frame_count;
    size_t retained_bytes;
    size_t pending_frame_count;
    size_t pending_bytes;
    bool graphics_lost;
};

static void call_callback(JSContext *ctx, JSValue callbacks, const char *name,
                          int argc, JSValue *argv);

static void set_error(JsVideoBindings *video, const char *message)
{
    snprintf(video->error, sizeof(video->error), "%s", message ? message : "");
}

static void invalidate_retained_frames(JsVideoBindings *video)
{
    for (size_t i = 0; i < VIDEO_RETAINED_FRAME_LIMIT; ++i)
    {
        JsVideoFrame *frame = &video->frames[i];
        if (!frame->id)
            continue;
        video->retained_frame_count--;
        video->retained_bytes -= frame->bytes;
        memset(frame, 0, sizeof(*frame));
    }
}

static bool read_path(JSContext *ctx, JSValueConst value, bool writable,
                      char *out, size_t out_size)
{
    size_t length = 0;
    const char *path = JS_ToCStringLen(ctx, &length, value);
    if (!path)
        return false;
    bool valid = length > 0 && length == strlen(path) && length < out_size &&
                 file_virtual_path_is_valid(path, false, writable) &&
                 ((!writable && (strncmp(path, "assets/", 7) == 0 ||
                                  strncmp(path, "files/", 6) == 0)) ||
                  (writable && strncmp(path, "files/", 6) == 0));
    if (valid)
        snprintf(out, out_size, "%s", path);
    JS_FreeCString(ctx, path);
    if (!valid)
        JS_ThrowTypeError(ctx, "%s path must be a safe path under %s",
                          writable ? "Output" : "Input",
                          writable ? "files/" : "assets/ or files/");
    return valid;
}

static bool read_callbacks(JSContext *ctx, JSValueConst value,
                           const char *const *names, size_t count)
{
    if (!JS_IsObject(value))
    {
        JS_ThrowTypeError(ctx, "Expected a callbacks object");
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        JSValue callback = JS_GetPropertyStr(ctx, value, names[i]);
        if (JS_IsException(callback))
            return false;
        bool valid = JS_IsFunction(ctx, callback);
        JS_FreeValue(ctx, callback);
        if (!valid)
        {
            JS_ThrowTypeError(ctx, "%s must be a function", names[i]);
            return false;
        }
    }
    return true;
}

static bool read_optional_callbacks(JSContext *ctx, JSValueConst value,
                                    const char *const *names, size_t count)
{
    if (!JS_IsObject(value))
    {
        JS_ThrowTypeError(ctx, "Expected a callbacks object");
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        JSValue callback = JS_GetPropertyStr(ctx, value, names[i]);
        if (JS_IsException(callback))
            return false;
        bool valid = JS_IsUndefined(callback) || JS_IsFunction(ctx, callback);
        JS_FreeValue(ctx, callback);
        if (!valid)
        {
            JS_ThrowTypeError(ctx, "%s must be a function when provided",
                              names[i]);
            return false;
        }
    }
    return true;
}

static bool read_handle(JSContext *ctx, JSValueConst value, uint32_t *out)
{
    double number;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) < 0 ||
        !isfinite(number) || number < 1 || number > UINT32_MAX ||
        floor(number) != number)
    {
        JS_ThrowRangeError(ctx, "Expected a positive safe video handle");
        return false;
    }
    *out = (uint32_t)number;
    return true;
}

static uint32_t next_handle(JsVideoBindings *video)
{
    if (video->next_id == UINT32_MAX)
        return 0;
    return ++video->next_id;
}

static JsVideoReader *find_reader(JsVideoBindings *video, uint32_t id)
{
    for (size_t i = 0; i < VIDEO_READER_LIMIT; ++i)
        if (video->readers[i].id == id)
            return &video->readers[i];
    return NULL;
}

static JsVideoFrame *find_frame(JsVideoBindings *video, uint32_t id)
{
    for (size_t i = 0; i < VIDEO_RETAINED_FRAME_LIMIT; ++i)
        if (video->frames[i].id == id)
            return &video->frames[i];
    return NULL;
}

static bool read_ordinal(JSContext *ctx, JSValueConst value, uint64_t *out)
{
    double number;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) < 0 ||
        !isfinite(number) || number < 0 ||
        number > 9007199254740991.0 || floor(number) != number)
    {
        JS_ThrowRangeError(ctx, "Expected a nonnegative safe frame ordinal");
        return false;
    }
    *out = (uint64_t)number;
    return true;
}

static void call_error_callback(JSContext *ctx, JSValue callbacks,
                                const char *name, const char *code,
                                const char *message)
{
    JSValue error = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, error, "code", JS_NewString(ctx, code));
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
    call_callback(ctx, callbacks, name, 1, &error);
    JS_FreeValue(ctx, error);
}

static JSValue js_video_open_reader(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic,
                                    JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    if (!graphics || !graphics->video_files || !*graphics->video_files)
        return JS_ThrowInternalError(ctx, "Video file subsystem is unavailable");
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected a video path and callbacks");
    const char *names[] = {"onReady", "onError"};
    if (!read_callbacks(ctx, argv[1], names, 2))
        return JS_EXCEPTION;
    char path[FILE_MAX_PATH];
    if (!read_path(ctx, argv[0], false, path, sizeof(path)))
        return JS_EXCEPTION;

    JsVideoBindings *video = graphics->video;
    JsVideoReader *slot = NULL;
    for (size_t i = 0; i < VIDEO_READER_LIMIT; ++i)
        if (!video->readers[i].id)
        {
            slot = &video->readers[i];
            break;
        }
    if (!slot)
    {
        set_error(video, "Video reader limit reached");
        return JS_NewUint32(ctx, 0);
    }
    uint32_t id = next_handle(video);
    if (!id)
    {
        set_error(video, "Video handle space exhausted");
        return JS_NewUint32(ctx, 0);
    }
    char error[256] = {0};
    VideoReader *reader = video_reader_open_file(
        *graphics->video_files, path, error, sizeof(error));
    if (!reader)
    {
        set_error(video, error);
        return JS_NewUint32(ctx, 0);
    }
    *slot = (JsVideoReader){
        .id = id,
        .reader = reader,
        .callbacks = JS_DupValue(ctx, argv[1]),
        .request_callbacks = JS_UNDEFINED,
    };
    set_error(video, "");
    return JS_NewUint32(ctx, id);
}

static JSValue js_video_request_frame(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    uint64_t ordinal;
    if (!graphics || argc < 3 || !read_handle(ctx, argv[0], &id) ||
        !read_ordinal(ctx, argv[1], &ordinal))
        return JS_EXCEPTION;
    const char *names[] = {"onFrame", "onEof", "onError"};
    if (!read_callbacks(ctx, argv[2], names, 3))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    JsVideoReader *reader = find_reader(video, id);
    if (!reader || reader->closing || !reader->ready)
    {
        set_error(video, "Video reader is unknown, closing, or not ready");
        return JS_FALSE;
    }
    if (video->graphics_lost)
    {
        set_error(video, "Video graphics context is lost");
        return JS_FALSE;
    }
    size_t bytes = (size_t)reader->track.width *
                   (size_t)reader->track.height * 4u;
    if (video->retained_frame_count + video->pending_frame_count >=
            VIDEO_RETAINED_FRAME_LIMIT ||
        bytes > VIDEO_RETAINED_BYTES_LIMIT ||
        video->retained_bytes + video->pending_bytes >
            VIDEO_RETAINED_BYTES_LIMIT - bytes)
    {
        set_error(video, "Video frame capacity is exhausted");
        return JS_FALSE;
    }
    char error[256] = {0};
    if (!video_reader_request_frame(reader->reader, ordinal, error,
                                   sizeof(error)))
    {
        set_error(video, error);
        return JS_FALSE;
    }
    reader->request_callbacks = JS_DupValue(ctx, argv[2]);
    reader->request_pending = true;
    reader->request_context_lost = false;
    reader->frame_cancel_requested = false;
    reader->reserved_bytes = bytes;
    video->pending_frame_count++;
    video->pending_bytes += bytes;
    set_error(video, "");
    return JS_TRUE;
}

static JSValue js_video_release_frame(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    JsVideoFrame *frame = find_frame(video, id);
    if (!frame)
    {
        set_error(video, "Unknown or already released video frame handle");
        return JS_FALSE;
    }
    if (graphics->window &&
        !video_texture_close(graphics->window, &frame->texture))
    {
        set_error(video, "Cannot release video frame texture");
        return JS_FALSE;
    }
    video->retained_frame_count--;
    video->retained_bytes -= frame->bytes;
    memset(frame, 0, sizeof(*frame));
    set_error(video, "");
    return JS_TRUE;
}

static JSValue js_video_cancel_frame_request(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv, int magic,
                                             JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    JsVideoReader *reader = find_reader(video, id);
    if (!reader || reader->closing || !reader->request_pending ||
        !video_reader_cancel_frame(reader->reader))
    {
        set_error(video, "No cancellable video frame request");
        return JS_FALSE;
    }
    reader->frame_cancel_requested = true;
    set_error(video, "");
    return JS_TRUE;
}

static JSValue js_video_close_reader(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv, int magic,
                                     JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    JsVideoReader *reader = find_reader(video, id);
    if (!reader || reader->closing)
    {
        set_error(video, "Unknown or inactive video reader handle");
        return JS_FALSE;
    }
    reader->closing = true;
    reader->open_cancel_pending = !reader->ready;
    reader->frame_cancel_pending = reader->request_pending;
    video_reader_request_close(reader->reader);
    set_error(video, "");
    return JS_TRUE;
}

static VideoCompositionConfig demo_config(void)
{
    return (VideoCompositionConfig){
        .output_width = VIDEO_OUTPUT_WIDTH,
        .output_height = VIDEO_OUTPUT_HEIGHT,
        .inset_x = VIDEO_INSET_X,
        .inset_y = VIDEO_INSET_Y,
        .inset_width = VIDEO_INSET_WIDTH,
        .inset_height = VIDEO_INSET_HEIGHT,
    };
}

static void call_callback(JSContext *ctx, JSValue callbacks, const char *name,
                          int argc, JSValue *argv)
{
    JSValue callback = JS_GetPropertyStr(ctx, callbacks, name);
    if (JS_IsFunction(ctx, callback))
    {
        JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, argc, argv);
        if (JS_IsException(result))
            js_dump_error(ctx);
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, callback);
}

static JSValue js_video_preview(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv, int magic,
                                JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    if (!graphics || !graphics->video_files || !*graphics->video_files)
        return JS_ThrowInternalError(ctx, "Video file subsystem is unavailable");
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected two input paths and callbacks");
    const char *names[] = {"onFrame", "onEnd", "onError"};
    if (!read_callbacks(ctx, argv[2], names, 3))
        return JS_EXCEPTION;
    const char *optional_names[] = {"onReady"};
    if (!read_optional_callbacks(ctx, argv[2], optional_names, 1))
        return JS_EXCEPTION;
    char background[FILE_MAX_PATH], inset[FILE_MAX_PATH];
    if (!read_path(ctx, argv[0], false, background, sizeof(background)) ||
        !read_path(ctx, argv[1], false, inset, sizeof(inset)))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    if (video->graphics_lost)
    {
        set_error(video, "Video graphics context is lost");
        return JS_NewUint32(ctx, 0);
    }
    if (video->preview.id || video->preview.closing)
    {
        set_error(video, "A video preview is already active");
        return JS_NewUint32(ctx, 0);
    }
    uint32_t id = next_handle(video);
    if (!id)
    {
        set_error(video, "Video handle space exhausted");
        return JS_NewUint32(ctx, 0);
    }
    char error[256] = {0};
    VideoCompositionConfig config = demo_config();
    VideoPlayer *player = video_player_open_files(
        *graphics->video_files, background, inset, &config, error,
        sizeof(error));
    if (!player)
    {
        set_error(video, error);
        return JS_NewUint32(ctx, 0);
    }
    video->preview = (JsVideoPreview){
        .id = id,
        .player = player,
        .callbacks = JS_DupValue(ctx, argv[2]),
    };
    snprintf(video->preview.background_path, sizeof(video->preview.background_path),
             "%s", background);
    snprintf(video->preview.inset_path, sizeof(video->preview.inset_path),
             "%s", inset);
    set_error(video, "");
    return JS_NewUint32(ctx, id);
}

static JSValue js_video_close_preview(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv, int magic,
                                       JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    if (video->preview.id != id ||
        (video->preview.closing && !video->preview.seeking))
    {
        set_error(video, "Unknown or inactive video preview handle");
        return JS_FALSE;
    }
    video->preview.closing = true;
    video->preview.seeking = false;
    video->preview.context_error_pending = false;
    video_player_request_close(video->preview.player);
    set_error(video, "");
    return JS_TRUE;
}

static JSValue js_video_set_preview_paused(JSContext *ctx, JSValueConst *argv,
                                           int argc, JSValue *func_data,
                                           bool paused)
{
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    if (!video->preview.player || video->preview.id != id ||
        (video->preview.closing && !video->preview.seeking))
    {
        set_error(video, "Unknown or inactive video preview handle");
        return JS_FALSE;
    }
    if (video->preview.seeking)
    {
        if (video->preview.paused == paused)
        {
            set_error(video, paused ? "Video preview is already paused"
                                    : "Video preview is not paused");
            return JS_FALSE;
        }
        video->preview.paused = paused;
        set_error(video, "");
        return JS_TRUE;
    }
    int64_t now = graphics->window
                      ? (int64_t)llround(window_get_time(graphics->window) *
                                         1000000.0)
                      : 0;
    char error[256] = {0};
    bool success = paused
                       ? video_player_pause(video->preview.player, now, error,
                                            sizeof(error))
                       : video_player_resume(video->preview.player, now, error,
                                             sizeof(error));
    if (!success)
        set_error(video, error);
    else
    {
        video->preview.paused = paused;
        set_error(video, "");
    }
    return success ? JS_TRUE : JS_FALSE;
}

static JSValue js_video_seek_preview(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv, int magic,
                                     JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    double number;
    if (argc < 2 || !JS_IsNumber(argv[1]) ||
        JS_ToFloat64(ctx, &number, argv[1]) < 0 || !isfinite(number) ||
        number < 0 || number > 9007199254740991.0 || floor(number) != number)
        return JS_ThrowRangeError(ctx, "Expected a nonnegative safe integer timestamp in microseconds");
    JsVideoPreview *preview = &graphics->video->preview;
    if (preview->id != id || !preview->player || !preview->ready ||
        preview->context_invalid || graphics->video->graphics_lost ||
        (preview->closing && !preview->seeking))
    {
        set_error(graphics->video, "Video preview is not ready to seek");
        return JS_FALSE;
    }
    preview->seek_us = (int64_t)number;
    if (preview->seek_us >= preview->duration_us)
        preview->seek_us = preview->duration_us - 1;
    preview->seeking = true;
    preview->closing = true;
    preview->terminal = false;
    video_player_request_close(preview->player);
    set_error(graphics->video, "");
    return JS_TRUE;
}

static JSValue js_video_pause_preview(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv, int magic,
                                       JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    return js_video_set_preview_paused(ctx, argv, argc, func_data, true);
}

static JSValue js_video_resume_preview(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv,
                                        int magic, JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    return js_video_set_preview_paused(ctx, argv, argc, func_data, false);
}

static JSValue js_video_export(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv, int magic,
                               JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    if (!graphics || !graphics->video_files || !*graphics->video_files)
        return JS_ThrowInternalError(ctx, "Video file subsystem is unavailable");
    if (argc < 4)
        return JS_ThrowTypeError(ctx, "Expected two input paths, output path and callbacks");
    const char *names[] = {"onComplete", "onError"};
    if (!read_callbacks(ctx, argv[3], names, 2))
        return JS_EXCEPTION;
    const char *optional_names[] = {"onProgress"};
    if (!read_optional_callbacks(ctx, argv[3], optional_names, 1))
        return JS_EXCEPTION;
    char background[FILE_MAX_PATH], inset[FILE_MAX_PATH], output[FILE_MAX_PATH];
    if (!read_path(ctx, argv[0], false, background, sizeof(background)) ||
        !read_path(ctx, argv[1], false, inset, sizeof(inset)) ||
        !read_path(ctx, argv[2], true, output, sizeof(output)))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    if (video->export.id || video->writer.id)
    {
        set_error(video, "A video export is already active");
        return JS_NewUint32(ctx, 0);
    }
    uint32_t id = next_handle(video);
    if (!id)
    {
        set_error(video, "Video handle space exhausted");
        return JS_NewUint32(ctx, 0);
    }
    char error[256] = {0};
    VideoCompositionConfig config = demo_config();
    VideoExport *job = video_export_start_files(
        *graphics->video_files, background, inset, output, &config,
        VIDEO_BIT_RATE, error, sizeof(error));
    if (!job)
    {
        set_error(video, error);
        return JS_NewUint32(ctx, 0);
    }
    video->export = (JsVideoExport){
        .id = id,
        .export = job,
        .callbacks = JS_DupValue(ctx, argv[3]),
    };
    snprintf(video->export.output_path, sizeof(video->export.output_path),
             "%s", output);
    set_error(video, "");
    return JS_NewUint32(ctx, id);
}

static JSValue js_video_cancel_export(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1 || !read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    JsVideoBindings *video = graphics->video;
    if (video->export.id != id || !video_export_cancel(video->export.export))
    {
        set_error(video, "Video export cancellation was not accepted");
        return JS_FALSE;
    }
    set_error(video, "");
    return JS_TRUE;
}

static JSValue js_video_get_error(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic,
                                  JSValue *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    return JS_NewString(ctx, graphics && graphics->video ? graphics->video->error : "");
}

static bool read_writer_option(JSContext *ctx, JSValueConst options,
                               const char *name, int fallback, int *out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    if (JS_IsException(value))
        return false;
    double number = fallback;
    bool valid = JS_IsUndefined(value) ||
        (JS_IsNumber(value) && JS_ToFloat64(ctx, &number, value) == 0 &&
         isfinite(number) && number >= 0 && number <= INT32_MAX &&
         floor(number) == number);
    JS_FreeValue(ctx, value);
    if (!valid)
    {
        JS_ThrowRangeError(ctx, "%s must be a nonnegative integer", name);
        return false;
    }
    *out = (int)number;
    return true;
}

static JSValue js_video_open_frame_writer(
    JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    if (!graphics || !graphics->video_files || !*graphics->video_files)
        return JS_ThrowInternalError(ctx, "Video file subsystem is unavailable");
    if (argc < 3 || !JS_IsObject(argv[1]))
        return JS_ThrowTypeError(ctx, "Expected output path, writer options and callbacks");
    const char *names[] = {"onReady", "onComplete", "onError"};
    char path[FILE_MAX_PATH];
    if (!read_path(ctx, argv[0], true, path, sizeof(path)) ||
        !read_callbacks(ctx, argv[2], names, 3))
        return JS_EXCEPTION;
    VideoWriterConfig config = {0};
    int total_frames;
    if (!read_writer_option(ctx, argv[1], "width", 0, &config.width) ||
        !read_writer_option(ctx, argv[1], "height", 0, &config.height) ||
        !read_writer_option(ctx, argv[1], "frameRateNum", 30, &config.frame_rate_num) ||
        !read_writer_option(ctx, argv[1], "frameRateDen", 1, &config.frame_rate_den) ||
        !read_writer_option(ctx, argv[1], "totalFrames", 0, &total_frames) ||
        !read_writer_option(ctx, argv[1], "videoBitRate", 8000000, &config.video_bit_rate) ||
        !read_writer_option(ctx, argv[1], "audioSampleRate", 0, &config.audio_sample_rate) ||
        !read_writer_option(ctx, argv[1], "audioChannels", 0, &config.audio_channels) ||
        !read_writer_option(ctx, argv[1], "audioBitRate",
                            config.audio_channels ? 128000 : 0, &config.audio_bit_rate))
        return JS_EXCEPTION;
    if (!video_frame_writer_valid_config(&config, total_frames))
        return JS_ThrowRangeError(ctx, "Invalid video frame writer options");
    JsVideoBindings *video = graphics->video;
    if (video->writer.id || video->export.id)
    {
        set_error(video, "A video export is already active");
        return JS_NewUint32(ctx, 0);
    }
    uint32_t id = next_handle(video);
    if (!id)
    {
        set_error(video, "Video handle space exhausted");
        return JS_NewUint32(ctx, 0);
    }
    char error[256] = {0};
    VideoFrameWriter *worker = video_frame_writer_open(
        *graphics->video_files, path, &config, total_frames, error, sizeof(error));
    if (!worker)
    {
        set_error(video, error);
        return JS_NewUint32(ctx, 0);
    }
    video->writer = (JsVideoFrameWriter){
        .id = id, .worker = worker, .callbacks = JS_DupValue(ctx, argv[2]),
    };
    snprintf(video->writer.output_path, sizeof(video->writer.output_path),
             "%s", path);
    set_error(video, "");
    return JS_NewUint32(ctx, id);
}

static const uint8_t *read_writer_buffer(JSContext *ctx, JSValueConst value,
                                        bool audio, size_t *size,
                                        JSValue *owner)
{
    *owner = JS_UNDEFINED;
    if (!audio && JS_IsArrayBuffer(value))
    {
        const uint8_t *bytes = JS_GetArrayBuffer(ctx, size, value);
        if (!bytes)
            JS_ThrowTypeError(ctx, "Video buffer is detached or invalid");
        return bytes;
    }
    int type = JS_GetTypedArrayType(value);
    if (audio ? type != JS_TYPED_ARRAY_FLOAT32 :
        (type != JS_TYPED_ARRAY_UINT8 && type != JS_TYPED_ARRAY_UINT8C))
    {
        JS_ThrowTypeError(ctx, audio ? "Expected Float32Array PCM" :
                          "Expected ArrayBuffer, Uint8Array or Uint8ClampedArray RGBA");
        return NULL;
    }
    size_t offset, element_size, buffer_size;
    *owner = JS_GetTypedArrayBuffer(ctx, value, &offset, size, &element_size);
    if (JS_IsException(*owner))
        return NULL;
    uint8_t *base = JS_GetArrayBuffer(ctx, &buffer_size, *owner);
    if (!base || offset > buffer_size || *size > buffer_size - offset)
    {
        JS_ThrowTypeError(ctx, "Video buffer is detached or invalid");
        return NULL;
    }
    return base + offset;
}

static JSValue js_video_write_frame(
    JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    uint64_t ordinal;
    if (!graphics || argc < 3)
        return JS_ThrowTypeError(ctx, "Expected writer, frame ordinal and RGBA pixels");
    if (!read_handle(ctx, argv[0], &id) || !read_ordinal(ctx, argv[1], &ordinal))
        return JS_EXCEPTION;
    if (ordinal >= 1000000)
        return JS_ThrowRangeError(ctx, "Frame ordinal must be below 1000000");
    JsVideoBindings *video = graphics->video;
    if (video->writer.id != id)
    {
        set_error(video, "Video frame writer is inactive");
        return JS_FALSE;
    }
    JSValue pixel_owner = JS_UNDEFINED, audio_owner = JS_UNDEFINED;
    size_t pixel_bytes = 0, audio_bytes = 0;
    const uint8_t *pixels = read_writer_buffer(ctx, argv[2], false,
                                              &pixel_bytes, &pixel_owner);
    const uint8_t *audio = NULL;
    bool have_audio = argc > 3 && !JS_IsUndefined(argv[3]);
    if (pixels && have_audio)
        audio = read_writer_buffer(ctx, argv[3], true, &audio_bytes, &audio_owner);
    bool valid = pixels && (!have_audio || audio);
    char error[256] = {0};
    bool accepted = valid && video_frame_writer_submit(
        video->writer.worker, (int)ordinal, pixels, pixel_bytes,
        (const float *)audio, audio_bytes / sizeof(float), error, sizeof(error));
    JS_FreeValue(ctx, pixel_owner);
    JS_FreeValue(ctx, audio_owner);
    if (!valid)
        return JS_EXCEPTION;
    set_error(video, error);
    return accepted ? JS_TRUE : JS_FALSE;
}

static JSValue js_video_finish_frame_writer(
    JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1)
        return JS_ThrowTypeError(ctx, "Expected a video frame writer handle");
    if (!read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    char error[256] = {0};
    bool accepted = graphics->video->writer.id == id &&
        video_frame_writer_finish(graphics->video->writer.worker, error, sizeof(error));
    set_error(graphics->video, accepted ? "" :
              (error[0] ? error : "Video frame writer is inactive"));
    return accepted ? JS_TRUE : JS_FALSE;
}

static JSValue js_video_cancel_frame_writer(
    JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
    int magic, JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSGraphicContext *graphics = js_graphic_context(ctx, func_data);
    uint32_t id;
    if (!graphics || argc < 1)
        return JS_ThrowTypeError(ctx, "Expected a video frame writer handle");
    if (!read_handle(ctx, argv[0], &id))
        return JS_EXCEPTION;
    bool accepted = graphics->video->writer.id == id &&
        video_frame_writer_cancel(graphics->video->writer.worker);
    if (accepted)
        graphics->video->writer.cancelling = true;
    set_error(graphics->video, accepted ? "" :
              "Video frame writer cancellation was not accepted");
    return accepted ? JS_TRUE : JS_FALSE;
}

static void poll_frame_writer(JsVideoBindings *video)
{
    JsVideoFrameWriter *writer = &video->writer;
    VideoFrameWriterEvent event;
    if (!writer->id || !video_frame_writer_poll(writer->worker, &event))
        return;
    if (event.status == VIDEO_FRAME_WRITER_READY)
    {
        if (writer->cancelling)
            return;
        JSValue callbacks = JS_DupValue(video->ctx, writer->callbacks);
        JSValue count = JS_NewInt32(video->ctx, event.completed_frames);
        call_callback(video->ctx, callbacks, "onReady", 1, &count);
        JS_FreeValue(video->ctx, count);
        JS_FreeValue(video->ctx, callbacks);
        return;
    }
    VideoFrameWriter *completed = writer->worker;
    JSValue callbacks = writer->callbacks;
    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s", writer->output_path);
    *writer = (JsVideoFrameWriter){.callbacks = JS_UNDEFINED};
    video_frame_writer_close(completed);
    if (event.status == VIDEO_FRAME_WRITER_COMPLETE)
    {
        JSValue value = JS_NewString(video->ctx, path);
        call_callback(video->ctx, callbacks, "onComplete", 1, &value);
        JS_FreeValue(video->ctx, value);
    }
    else
        call_error_callback(video->ctx, callbacks, "onError",
                            event.status == VIDEO_FRAME_WRITER_CANCELLED
                                ? "video.cancelled" : "video.export_failed",
                            event.message);
    JS_FreeValue(video->ctx, callbacks);
}

static JSValue track_info_value(JSContext *ctx, const VideoTrack *track,
                                int64_t duration_us)
{
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "width", JS_NewInt32(ctx, track->width));
    JS_SetPropertyStr(ctx, info, "height", JS_NewInt32(ctx, track->height));
    JS_SetPropertyStr(ctx, info, "frameRateNum",
                      JS_NewInt32(ctx, track->frame_rate_num));
    JS_SetPropertyStr(ctx, info, "frameRateDen",
                      JS_NewInt32(ctx, track->frame_rate_den));
    JS_SetPropertyStr(ctx, info, "sampleAspectRatioNum",
                      JS_NewInt32(ctx, track->sample_aspect_ratio_num));
    JS_SetPropertyStr(ctx, info, "sampleAspectRatioDen",
                      JS_NewInt32(ctx, track->sample_aspect_ratio_den));
    JS_SetPropertyStr(ctx, info, "rotationDegrees",
                      JS_NewInt32(ctx, track->rotation_degrees));
    JS_SetPropertyStr(ctx, info, "timeBaseNum",
                      JS_NewInt32(ctx, track->time_base_num));
    JS_SetPropertyStr(ctx, info, "timeBaseDen",
                      JS_NewInt32(ctx, track->time_base_den));
    JS_SetPropertyStr(ctx, info, "durationUs",
                      JS_NewInt64(ctx, duration_us));
    JS_SetPropertyStr(ctx, info, "seekable", JS_TRUE);
    JS_SetPropertyStr(ctx, info, "exactFrameOrdinals", JS_TRUE);
    JS_SetPropertyStr(ctx, info, "timeOrigin",
                      JS_NewString(ctx, "stream-start"));
    JS_SetPropertyStr(ctx, info, "codec", JS_NewString(ctx, track->codec));
    JS_SetPropertyStr(ctx, info, "colorPrimaries",
                      JS_NewString(ctx, "bt709"));
    JS_SetPropertyStr(ctx, info, "colorTransfer",
                      JS_NewString(ctx, "bt709"));
    JS_SetPropertyStr(ctx, info, "colorMatrix",
                      JS_NewString(ctx, "bt709"));
    JS_SetPropertyStr(ctx, info, "colorRange",
                      JS_NewString(ctx,
                          track->color_range == VIDEO_COLOR_RANGE_FULL
                              ? "full" : "limited"));
    return info;
}

static void poll_single_readers(JSGraphicContext *graphics)
{
    JsVideoBindings *video = graphics->video;
    for (size_t i = 0; i < VIDEO_READER_LIMIT; ++i)
    {
        JsVideoReader *reader = &video->readers[i];
        if (!reader->id)
            continue;
        if (reader->closing)
        {
            if (reader->open_cancel_pending)
            {
                reader->open_cancel_pending = false;
                JSValue callbacks = JS_DupValue(video->ctx, reader->callbacks);
                call_error_callback(video->ctx, callbacks, "onError",
                                    "video.cancelled", "Video reader was closed");
                JS_FreeValue(video->ctx, callbacks);
            }
            if (reader->frame_cancel_pending)
            {
                reader->frame_cancel_pending = false;
                JSValue callbacks = reader->request_callbacks;
                reader->request_callbacks = JS_UNDEFINED;
                call_error_callback(video->ctx, callbacks, "onError",
                                    "video.cancelled", "Video reader was closed");
                JS_FreeValue(video->ctx, callbacks);
            }
            if (!video_reader_stopped(reader->reader))
                continue;
            if (reader->request_pending)
            {
                video->pending_frame_count--;
                video->pending_bytes -= reader->reserved_bytes;
            }
            video_reader_close(reader->reader);
            JS_FreeValue(video->ctx, reader->callbacks);
            if (reader->request_pending)
                JS_FreeValue(video->ctx, reader->request_callbacks);
            memset(reader, 0, sizeof(*reader));
            reader->callbacks = JS_UNDEFINED;
            reader->request_callbacks = JS_UNDEFINED;
            continue;
        }

        VideoReaderEvent event;
        if (!video_reader_poll(reader->reader, &event))
            continue;
        if (event.type == VIDEO_READER_READY)
        {
            reader->track = event.track;
            reader->ready = true;
            JSValue callbacks = JS_DupValue(video->ctx, reader->callbacks);
            JSValue info = track_info_value(video->ctx, &event.track,
                                            event.track_duration_us);
            call_callback(video->ctx, callbacks, "onReady", 1, &info);
            JS_FreeValue(video->ctx, info);
            JS_FreeValue(video->ctx, callbacks);
        }
        else if (event.type == VIDEO_READER_ERROR &&
                 !reader->request_pending)
        {
            reader->ready = true;
            JSValue callbacks = JS_DupValue(video->ctx, reader->callbacks);
            call_error_callback(video->ctx, callbacks, "onError",
                                event.code, event.message);
            JS_FreeValue(video->ctx, callbacks);
        }
        else if (reader->request_pending)
        {
            JSValue callbacks = JS_DupValue(video->ctx,
                                             reader->request_callbacks);
            JS_FreeValue(video->ctx, reader->request_callbacks);
            reader->request_callbacks = JS_UNDEFINED;
            reader->request_pending = false;
            size_t reserved_bytes = reader->reserved_bytes;
            video->pending_frame_count--;
            video->pending_bytes -= reserved_bytes;
            reader->reserved_bytes = 0;
            bool context_lost = reader->request_context_lost;
            reader->request_context_lost = false;
            bool cancelled = reader->frame_cancel_requested;
            reader->frame_cancel_requested = false;
            if (context_lost && !cancelled)
                call_error_callback(video->ctx, callbacks, "onError",
                                    "video.graphics_context_lost",
                                    "Graphics context was lost during the video frame request");
            else if (event.type == VIDEO_READER_FRAME)
            {
                JsVideoFrame *frame_slot = NULL;
                for (size_t j = 0; j < VIDEO_RETAINED_FRAME_LIMIT; ++j)
                    if (!video->frames[j].id)
                    {
                        frame_slot = &video->frames[j];
                        break;
                    }
                char error[256] = {0};
                uint32_t frame_id = next_handle(video);
                bool uploaded = frame_slot && frame_id && graphics->window &&
                    video_texture_upload(graphics->window,
                                         &frame_slot->texture,
                                         &event.frame.frame,
                                         error, sizeof(error));
                if (!uploaded)
                {
                    if (!error[0])
                        snprintf(error, sizeof(error),
                                 "Video frame capacity is exhausted");
                    call_error_callback(video->ctx, callbacks, "onError",
                                        "video.texture_upload_failed", error);
                }
                else
                {
                    frame_slot->id = frame_id;
                    frame_slot->bytes = reserved_bytes;
                    video->retained_frame_count++;
                    video->retained_bytes += frame_slot->bytes;
                    JSValue args[] = {
                        JS_NewUint32(video->ctx, frame_id),
                        JS_NewInt32(video->ctx, frame_slot->texture.texture_id),
                        JS_NewInt64(video->ctx, event.pts_us),
                        JS_NewInt64(video->ctx, event.duration_us),
                        JS_NewInt64(video->ctx, (int64_t)event.ordinal),
                        JS_NewInt32(video->ctx, event.frame.frame.width),
                        JS_NewInt32(video->ctx, event.frame.frame.height),
                    };
                    call_callback(video->ctx, callbacks, "onFrame", 7, args);
                    for (size_t j = 0; j < sizeof(args) / sizeof(args[0]); ++j)
                        JS_FreeValue(video->ctx, args[j]);
                }
            }
            else if (event.type == VIDEO_READER_EOF)
                call_callback(video->ctx, callbacks, "onEof", 0, NULL);
            else
                call_error_callback(video->ctx, callbacks, "onError",
                                    event.code, event.message);
            JS_FreeValue(video->ctx, callbacks);
        }
        video_reader_event_free(&event);
    }
}

static bool video_readers_pending(const JsVideoBindings *video)
{
    for (size_t i = 0; i < VIDEO_READER_LIMIT; ++i)
    {
        const JsVideoReader *reader = &video->readers[i];
        if (reader->id &&
            (reader->closing || !reader->ready || reader->request_pending))
            return true;
    }
    return false;
}

void js_video_context_lost(JSGraphicContext *graphics)
{
    if (!graphics || !graphics->video)
        return;
    JsVideoBindings *video = graphics->video;
    video->graphics_lost = true;
    invalidate_retained_frames(video);
    for (size_t i = 0; i < VIDEO_READER_LIMIT; ++i)
        if (video->readers[i].request_pending && !video->readers[i].closing)
            video->readers[i].request_context_lost = true;

    JsVideoPreview *preview = &video->preview;
    if (preview->player)
    {
        video_player_context_lost(preview->player);
        preview->context_invalid = true;
        if (!preview->closing && !preview->terminal)
        {
            preview->terminal = true;
            preview->context_error_pending = true;
        }
        else if (preview->seeking)
        {
            preview->closing = false;
            preview->seeking = false;
            preview->terminal = true;
            preview->context_error_pending = true;
        }
    }
}

void js_video_context_restored(JSGraphicContext *graphics)
{
    if (graphics && graphics->video)
        graphics->video->graphics_lost = false;
}

static const JsGraphicFunction video_functions[] = {
    {"preview", 3, js_video_preview},
    {"closePreview", 1, js_video_close_preview},
    {"pausePreview", 1, js_video_pause_preview},
    {"resumePreview", 1, js_video_resume_preview},
    {"seekPreview", 2, js_video_seek_preview},
    {"exportMp4", 4, js_video_export},
    {"cancelExport", 1, js_video_cancel_export},
    {"getError", 0, js_video_get_error},
    {"openReader", 2, js_video_open_reader},
    {"requestFrame", 3, js_video_request_frame},
    {"cancelFrameRequest", 1, js_video_cancel_frame_request},
    {"releaseFrame", 1, js_video_release_frame},
    {"closeReader", 1, js_video_close_reader},
    {"openFrameWriter", 3, js_video_open_frame_writer},
    {"writeFrame", 4, js_video_write_frame},
    {"finishFrameWriter", 1, js_video_finish_frame_writer},
    {"cancelFrameWriter", 1, js_video_cancel_frame_writer},
};

void js_video_register(JSContext *ctx, JSValueConst sys_obj,
                       JSGraphicContext *graphics)
{
    JsVideoBindings *video = calloc(1, sizeof(*video));
    if (!video)
        return;
    video->ctx = ctx;
    video->preview.callbacks = JS_UNDEFINED;
    video->export.callbacks = JS_UNDEFINED;
    video->writer.callbacks = JS_UNDEFINED;
    graphics->video = video;
    JSValue object = JS_NewObject(ctx);
    for (size_t i = 0; i < sizeof(video_functions) / sizeof(video_functions[0]); ++i)
        js_graphic_register_function(ctx, object, &video_functions[i], graphics);
    JS_SetPropertyStr(ctx, sys_obj, "video", object);
}

static void finish_preview(JSGraphicContext *graphics)
{
    JsVideoBindings *video = graphics->video;
    JsVideoPreview *preview = &video->preview;
    if (!preview->closing)
        return;

    VideoPlayerCloseResult result = preview->player
        ? video_player_try_close(preview->player, graphics->window)
        : VIDEO_PLAYER_CLOSE_DONE;
    if (result == VIDEO_PLAYER_CLOSE_ERROR && !graphics->window)
    {

        video_player_close_without_graphics(preview->player);
        result = VIDEO_PLAYER_CLOSE_DONE;
    }
    if (result == VIDEO_PLAYER_CLOSE_DONE)
    {
        preview->player = NULL;
        if (preview->seeking)
        {
            char error[256] = {0};
            VideoCompositionConfig config = demo_config();
            if (graphics->video_files && *graphics->video_files)
                preview->player = video_player_open_files_at(
                    *graphics->video_files, preview->background_path,
                    preview->inset_path, &config, preview->seek_us,
                    preview->paused, error, sizeof(error));
            else
                snprintf(error, sizeof(error), "%s",
                         "Video file subsystem is unavailable");
            preview->seeking = false;
            preview->closing = false;
            if (preview->player)
                return;

            JSValue callbacks = JS_DupValue(video->ctx, preview->callbacks);
            JSValue details = JS_NewObject(video->ctx);
            JS_SetPropertyStr(video->ctx, details, "code",
                              JS_NewString(video->ctx, "video.open_failed"));
            JS_SetPropertyStr(video->ctx, details, "message",
                              JS_NewString(video->ctx, error));
            call_callback(video->ctx, callbacks, "onError", 1, &details);
            JS_FreeValue(video->ctx, details);
            JS_FreeValue(video->ctx, callbacks);
            return;
        }
        JS_FreeValue(video->ctx, preview->callbacks);
        memset(preview, 0, sizeof(*preview));
        preview->callbacks = JS_UNDEFINED;
    }
}

void js_video_poll(JSGraphicContext *graphics)
{
    if (!graphics || !graphics->video)
        return;
    JsVideoBindings *video = graphics->video;
    if (!graphics->window)
        invalidate_retained_frames(video);
    poll_single_readers(graphics);
    poll_frame_writer(video);
    if (video->preview.context_error_pending)
    {
        video->preview.context_error_pending = false;
        JSValue callbacks = JS_DupValue(video->ctx, video->preview.callbacks);
        call_error_callback(video->ctx, callbacks, "onError",
                            "video.graphics_context_lost",
                            "Graphics context was lost during video preview");
        JS_FreeValue(video->ctx, callbacks);
    }
    if (video->preview.player && !video->preview.closing)
    {
        VideoPlayerEvent event;
        int64_t now = graphics->window
                          ? (int64_t)llround(window_get_time(graphics->window) * 1000000.0)
                          : 0;
        if (video_player_poll(video->preview.player, graphics->window, now, &event))
        {
            JSValue callbacks = JS_DupValue(video->ctx, video->preview.callbacks);
            if (event.type == VIDEO_PLAYER_FRAME)
            {
                uint32_t id = video->preview.id;
                if (!video->preview.ready)
                {
                    video->preview.ready = true;
                    video->preview.duration_us = event.duration_us;
                    JSValue duration = JS_NewInt64(video->ctx, event.duration_us);
                    call_callback(video->ctx, callbacks, "onReady", 1, &duration);
                    JS_FreeValue(video->ctx, duration);
                }
                JSValue args[] = {JS_NewInt32(video->ctx, event.texture_id),
                                  JS_NewInt64(video->ctx, event.pts_us)};
                if (video->preview.id == id && !video->preview.closing)
                    call_callback(video->ctx, callbacks, "onFrame", 2, args);
                JS_FreeValue(video->ctx, args[0]);
                JS_FreeValue(video->ctx, args[1]);
            }
            else if (event.type == VIDEO_PLAYER_END)
            {
                video->preview.terminal = true;
                call_callback(video->ctx, callbacks, "onEnd", 0, NULL);
            }
            else
            {
                video->preview.terminal = true;
                JSValue error = JS_NewObject(video->ctx);
                JS_SetPropertyStr(video->ctx, error, "code",
                                  JS_NewString(video->ctx, event.code));
                JS_SetPropertyStr(video->ctx, error, "message",
                                  JS_NewString(video->ctx, event.message));
                call_callback(video->ctx, callbacks, "onError", 1, &error);
                JS_FreeValue(video->ctx, error);
            }
            JS_FreeValue(video->ctx, callbacks);
        }
    }
    finish_preview(graphics);

    if (video->export.export)
    {
        VideoExportEvent event;
        video_export_poll(video->export.export, &event);
        if (event.completed_frames > video->export.progress)
        {
            video->export.progress = event.completed_frames;
            JSValue callbacks = JS_DupValue(video->ctx, video->export.callbacks);
            JSValue progress = JS_NewInt64(video->ctx, event.completed_frames);
            call_callback(video->ctx, callbacks, "onProgress", 1, &progress);
            JS_FreeValue(video->ctx, progress);
            JS_FreeValue(video->ctx, callbacks);
        }
        if (event.status != VIDEO_EXPORT_RUNNING)
        {

            VideoExport *completed_export = video->export.export;
            JSValue callbacks = video->export.callbacks;
            char output_path[FILE_MAX_PATH];
            snprintf(output_path, sizeof(output_path), "%s",
                     video->export.output_path);
            video->export = (JsVideoExport){.callbacks = JS_UNDEFINED};
            video_export_close(completed_export);
            if (event.status == VIDEO_EXPORT_SUCCEEDED)
            {
                JSValue path = JS_NewString(video->ctx, output_path);
                call_callback(video->ctx, callbacks, "onComplete", 1, &path);
                JS_FreeValue(video->ctx, path);
            }
            else
            {
                JSValue error = JS_NewObject(video->ctx);
                JS_SetPropertyStr(video->ctx, error, "code",
                                  JS_NewString(video->ctx, event.code));
                JS_SetPropertyStr(video->ctx, error, "message",
                                  JS_NewString(video->ctx, event.message));
                call_callback(video->ctx, callbacks, "onError", 1, &error);
                JS_FreeValue(video->ctx, error);
            }
            JS_FreeValue(video->ctx, callbacks);
        }
    }
}

bool js_video_pending(JSGraphicContext *graphics)
{
    if (!graphics || !graphics->video)
        return false;
    return graphics->video->preview.closing ||
           graphics->video->preview.context_error_pending ||
           video_readers_pending(graphics->video) ||
           (graphics->video->preview.player &&
            video_player_pending(graphics->video->preview.player)) ||
           graphics->video->export.export ||
           graphics->video->writer.worker;
}

void js_video_cleanup(JSGraphicContext *graphics)
{
    if (!graphics || !graphics->video)
        return;
    JsVideoBindings *video = graphics->video;
    for (size_t i = 0; i < VIDEO_READER_LIMIT; ++i)
    {
        JsVideoReader *reader = &video->readers[i];
        if (!reader->id)
            continue;
        video_reader_request_close(reader->reader);
        video_reader_close(reader->reader);
        JS_FreeValue(video->ctx, reader->callbacks);
        if (reader->request_pending)
            JS_FreeValue(video->ctx, reader->request_callbacks);
    }
    for (size_t i = 0; i < VIDEO_RETAINED_FRAME_LIMIT; ++i)
    {
        JsVideoFrame *frame = &video->frames[i];
        if (frame->id && graphics->window &&
            !video_texture_close(graphics->window, &frame->texture))
            fprintf(stderr, "Warning: could not destroy retained video frame texture during shutdown\n");
    }
    if (video->preview.player)
    {
        video_player_request_close(video->preview.player);
        bool closed = false;

        for (int attempt = 0; graphics->window && attempt < 3; ++attempt)
        {
            if (video_player_close(video->preview.player, graphics->window))
            {
                closed = true;
                break;
            }
        }
        if (!closed)
        {

            if (graphics->window)
                fprintf(stderr,
                        "Warning: could not destroy video preview texture during shutdown\n");
            video_player_close_without_graphics(video->preview.player);
        }
    }
    if (video->preview.id)
        JS_FreeValue(video->ctx, video->preview.callbacks);
    if (video->export.export)
    {
        video_export_close(video->export.export);
        JS_FreeValue(video->ctx, video->export.callbacks);
    }
    if (video->writer.worker)
    {
        video_frame_writer_close(video->writer.worker);
        JS_FreeValue(video->ctx, video->writer.callbacks);
    }
    free(video);
    graphics->video = NULL;
}