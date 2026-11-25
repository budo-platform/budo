#include "core/window.h"
#include "file/file_wrapper.h"
#include "graphics/js_video_bindings.h"
#include "video/video_demux.h"
#include "video/video_frame_writer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now = 5.0;
static int next_texture = 1;
static int live_textures;
static uint64_t texture_hashes[1024];

static JSValue evaluate(JSContext *ctx, const char *script);

static uint64_t hash_pixels(const uint8_t *pixels, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= pixels[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t frame_texture_hash(JSContext *ctx, int index)
{
    char expression[128];
    snprintf(expression, sizeof(expression),
             "ordinalFrames[%d].texture", index);
    JSValue result = evaluate(ctx, expression);
    int32_t texture = 0;
    assert(JS_ToInt32(ctx, &texture, result) == 0);
    JS_FreeValue(ctx, result);
    assert(texture > 0 && texture < (int)(sizeof(texture_hashes) /
                                           sizeof(texture_hashes[0])));
    return texture_hashes[texture];
}

double window_get_time(Window *window)
{
    (void)window;
    return now;
}

int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    (void)window;
    assert(((width == 640 && height == 360) ||
            (width == 64 && height == 48)) &&
           format == WINDOW_GL_TEX_RGBA8 && pixels);
    live_textures++;
    int texture = next_texture++;
    assert(texture > 0 && texture < (int)(sizeof(texture_hashes) /
                                           sizeof(texture_hashes[0])));
    texture_hashes[texture] = hash_pixels(
        pixels, (size_t)width * (size_t)height * 4u);
    return texture;
}

bool window_gl_update_texture_2d(Window *window, int texture, int x, int y,
                                 int width, int height, const void *pixels)
{
    (void)window;
    assert(texture > 0 && x == 0 && y == 0 && width == 640 && height == 360 && pixels);
    return true;
}

bool window_gl_destroy_texture(Window *window, int texture)
{
    (void)window;
    assert(texture > 0 && live_textures > 0);
    live_textures--;
    return true;
}

int js_graphic_register_function(JSContext *ctx, JSValue obj,
                                 const JsGraphicFunction *definition,
                                 JSGraphicContext *graphics)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&graphics,
                                         sizeof(graphics));
    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    return JS_SetPropertyStr(ctx, obj, definition->name, function);
}

JSGraphicContext *js_graphic_context(JSContext *ctx, JSValueConst *func_data)
{
    size_t size;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    JSGraphicContext *graphics = NULL;
    if (data && size == sizeof(graphics))
        memcpy(&graphics, data, size);
    return graphics;
}

void js_dump_error(JSContext *ctx)
{
    JSValue exception = JS_GetException(ctx);
    const char *message = JS_ToCString(ctx, exception);
    fprintf(stderr, "Video callback exception: %s\n", message ? message : "(unknown)");
    JS_FreeCString(ctx, message);
    JS_FreeValue(ctx, exception);
    assert(false);
}

static JSValue evaluate(JSContext *ctx, const char *script)
{
    JSValue result = JS_Eval(ctx, script, strlen(script), "video-test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
        js_dump_error(ctx);
    return result;
}

static void run(JSContext *ctx, const char *script)
{
    JSValue result = evaluate(ctx, script);
    JS_FreeValue(ctx, result);
}

static void wait_for(JSGraphicContext *graphics, const char *expression)
{
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        js_video_poll(graphics);
        JSValue result = evaluate(graphics->context, expression);
        int ready = JS_ToBool(graphics->context, result);
        JS_FreeValue(graphics->context, result);
        if (ready)
            return;
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    fprintf(stderr, "Timed out waiting for %s\n", expression);
    assert(false);
}

static void test_frame_writer(JSGraphicContext *graphics, FileContext *files)
{
    JSContext *ctx = graphics->context;
    run(ctx,
        "globalThis.writerDone = false; globalThis.writerError = null;"
        "globalThis.writerReady = 0; globalThis.cancelledWriter = null;"
        "const writerOptions = {width:64, height:48, totalFrames:480,"
        " audioSampleRate:48000, audioChannels:1};"
        "for (const options of [{...writerOptions,width:63},"
        " {...writerOptions,totalFrames:0},{...writerOptions,audioChannels:3},"
        " {...writerOptions,frameRateNum:NaN}]) {"
        " let threw = false; try { sys.video.openFrameWriter('files/invalid.mp4',options,"
        " {onReady(){},onComplete(){},onError(){}}); } catch(e) { threw = true; }"
        " if (!threw) throw Error('invalid writer config accepted');"
        "}"
        "const rgbaStorage = new Uint8Array(64*48*4+8);"
        "const rgba = rgbaStorage.subarray(4,rgbaStorage.length-4);"
        "const pcmStorage = new Float32Array(1602);"
        "const pcm = pcmStorage.subarray(1,1601);"
        "globalThis.writerId = sys.video.openFrameWriter('files/generated.mp4',writerOptions,{"
        " onReady(count) {"
        "  if (count !== writerReady++) throw Error('writer acknowledgement order');"
        "  if (count === 480) {"
        "   if (!sys.video.finishFrameWriter(writerId)) throw Error(sys.video.getError());"
        "   return;"
        "  }"
        "  if (count === 0) {"
        "   if (sys.video.finishFrameWriter(writerId)) throw Error('incomplete export accepted');"
        "   if (sys.video.writeFrame(writerId,1,rgba,pcm)) throw Error('out of order accepted');"
        "   if (sys.video.writeFrame(writerId,0,rgba.subarray(4),pcm)) throw Error('short pixels accepted');"
        "   if (sys.video.writeFrame(writerId,0,rgba,pcm.subarray(1))) throw Error('short audio accepted');"
        "   pcm[0] = NaN;"
        "   if (sys.video.writeFrame(writerId,0,rgba,pcm)) throw Error('NaN audio accepted');"
        "   pcm[0] = 2;"
        "   if (sys.video.writeFrame(writerId,0,rgba,pcm)) throw Error('clipped audio accepted');"
        "   let threw = false;"
        "   try {sys.video.writeFrame(writerId,0,rgba,new Int32Array(1600));}"
        "   catch(e) {threw=true;}"
        "   if (!threw) throw Error('integer PCM accepted');"
        "  }"
        "  for(let i=0;i<rgba.length;i+=4) {"
        "   rgba[i]=i<rgba.length/2?255:0; rgba[i+1]=0;"
        "   rgba[i+2]=i<rgba.length/2?0:255; rgba[i+3]=255;"
        "  }"
        "  for(let i=0;i<1600;i++) pcm[i]=0.1*Math.sin(2*Math.PI*400*(count*1600+i)/48000);"
        "  if (!sys.video.writeFrame(writerId,count,rgba,pcm)) throw Error(sys.video.getError());"
        "  rgba.fill(0); pcm.fill(0);"
        "  if (sys.video.writeFrame(writerId,count+1,rgba,pcm)) throw Error('backpressure not enforced');"
        " },"
        " onComplete(path) {"
        "  if (path !== 'files/generated.mp4') throw Error('wrong saved path');"
        "  writerDone = true;"
        "  cancelledWriter = sys.video.openFrameWriter('files/cancelled-generated.mp4',writerOptions,{"
        "   onReady(){throw Error('ready after cancellation');},onComplete(){throw Error('published cancelled output');},"
        "   onError(e){writerError=e;}"
        "  });"
        "  if(!cancelledWriter || !sys.video.cancelFrameWriter(cancelledWriter)) throw Error(sys.video.getError());"
        " },onError(e){writerError=e;}"
        "});"
        "if (!writerId) throw Error(sys.video.getError());"
        "if (sys.video.exportMp4('assets/background.mp4','assets/inset.mp4','files/parallel.mp4',"
        " {onComplete(){},onError(){}})) throw Error('parallel export accepted');");
    assert(js_video_pending(graphics));
    wait_for(graphics, "writerDone && writerError");
    run(ctx,
        "if(writerReady!==481 || writerError.code!=='video.cancelled') throw Error('writer lifecycle failed');"
        "if(sys.video.writeFrame(writerId,0,rgba,pcm) || sys.video.cancelFrameWriter(cancelledWriter))"
        " throw Error('terminal writer remained valid');");
    char error[256] = {0};
    VideoDemux *demux = video_demux_open_file(files, "files/generated.mp4",
                                             error, sizeof(error));
    assert(demux && video_demux_track_count(demux) == 2);
    VideoTrack track;
    assert(video_demux_track(demux, 0, &track));
    assert(strcmp(track.codec, "h264") == 0 && track.width == 64 &&
           track.height == 48 && track.frame_rate_num == 30 &&
           track.frame_rate_den == 1);
    assert(video_demux_decode_open(demux, 0, error, sizeof(error)));
    VideoFrame frame;
    int count = 0, result;
    int64_t last_pts = -1;
    while ((result = video_demux_decode_next(demux, &frame, error, sizeof(error))) == 1)
    {
        assert(frame.pts > last_pts);
        last_pts = frame.pts;
        assert(frame.rgba[0] > 230 && frame.rgba[2] < 20);
        size_t bottom = (size_t)(frame.height - 1) * frame.stride;
        assert(frame.rgba[bottom] < 20 && frame.rgba[bottom + 2] > 230);
        count++;
    }
    assert(result == 0 && count == 480);
    assert(last_pts * frame.time_base_num * 1000000 / frame.time_base_den == 15966666);
    video_demux_close(demux);
    demux = video_demux_open_file(files, "files/generated.mp4", error, sizeof(error));
    assert(demux && video_demux_track(demux, 1, &track));
    assert(strcmp(track.codec, "aac") == 0 && track.sample_rate == 48000 && track.channels == 1);
    assert(video_demux_audio_open(demux, 1, error, sizeof(error)));
    VideoAudioBlock block;
    bool audible = false;
    size_t audio_frames = 0;
    while ((result = video_demux_audio_next(demux, &block, error, sizeof(error))) == 1)
    {
        audio_frames += block.frame_count;
        for (size_t i = 0; i < block.frame_count; ++i)
            if (block.samples[i] > 0.05f)
                audible = true;
    }
    assert(result == 0 && audible && audio_frames >= 768000 && audio_frames <= 770048);
    video_demux_close(demux);
    assert(!video_demux_open_file(files, "files/cancelled-generated.mp4", error, sizeof(error)));

    VideoWriterConfig rational = {
        .width = 64, .height = 48, .frame_rate_num = 30000,
        .frame_rate_den = 1001, .video_bit_rate = 100000,
        .audio_sample_rate = 44100, .audio_channels = 2, .audio_bit_rate = 128000,
    };
    size_t total_samples = 0;
    for (int i = 0; i < 30000; ++i)
        total_samples += video_frame_writer_audio_frames(&rational, i);
    assert(total_samples == 44100u * 1001);
    run(ctx,
        "globalThis.rationalDone=false; globalThis.rationalError=null;"
        "globalThis.rationalId=sys.video.openFrameWriter('files/rational.mp4',"
        " {width:64,height:48,totalFrames:60,frameRateNum:30000,frameRateDen:1001,"
        " audioSampleRate:44100,audioChannels:2},{"
        " onReady(n){"
        "  if(n===60){if(!sys.video.finishFrameWriter(rationalId)) throw Error(sys.video.getError());return;}"
        "  const count=Math.round((n+1)*44100*1001/30000)-Math.round(n*44100*1001/30000);"
        "  if(!sys.video.writeFrame(rationalId,n,rgba,new Float32Array(count*2))) throw Error(sys.video.getError());"
        " },onComplete(){rationalDone=true;},onError(e){rationalError=e;}"
        "}); if(!rationalId) throw Error(sys.video.getError());");
    wait_for(graphics, "rationalDone || rationalError");
    run(ctx, "if(rationalError) throw Error(rationalError.message);");
    demux = video_demux_open_file(files, "files/rational.mp4", error, sizeof(error));
    assert(demux && video_demux_track(demux, 1, &track));
    assert(track.sample_rate == 44100 && track.channels == 2);
    assert(video_demux_decode_open(demux, 0, error, sizeof(error)));
    count = 0;
    while ((result = video_demux_decode_next(demux, &frame, error, sizeof(error))) == 1)
        count++;
    assert(result == 0 && count == 60);
    video_demux_close(demux);

    assert(file_write_text(files, "files/preserved.mp4", "keep", 4));
    run(ctx,
        "globalThis.inflightCancelled=null;"
        "globalThis.inflightId=sys.video.openFrameWriter('files/preserved.mp4',writerOptions,{"
        " onReady(n){"
        "  if(n!==0) throw Error('ready after inflight cancellation');"
        "  if(!sys.video.writeFrame(inflightId,0,rgba,pcm) || !sys.video.cancelFrameWriter(inflightId))"
        "   throw Error(sys.video.getError());"
        " },onComplete(){throw Error('cancelled output published');},onError(e){inflightCancelled=e;}"
        "}); if(!inflightId) throw Error(sys.video.getError());");
    wait_for(graphics, "inflightCancelled");
    run(ctx, "if(inflightCancelled.code!=='video.cancelled') throw Error(inflightCancelled.message);");
    size_t size;
    char *preserved = file_read_text(files, "files/preserved.mp4", &size);
    assert(preserved && size == 4 && memcmp(preserved, "keep", 4) == 0);
    free(preserved);
    run(ctx,
        "globalThis.sinkError=null;"
        "const badSink=sys.video.openFrameWriter('files/missing-directory/film.mp4',writerOptions,{"
        " onReady(){throw Error('missing sink became ready');},onComplete(){throw Error('missing sink succeeded');},"
        " onError(e){sinkError=e;}"
        "}); if(!badSink) throw Error(sys.video.getError());");
    wait_for(graphics, "sinkError");
    run(ctx, "if(sinkError.code!=='video.export_failed') throw Error('sink error not reported');");
    assert(!js_video_pending(graphics));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FileContext *files = file_create(argv[1]);
    assert(files);
    file_set_write_root(files, argv[1]);
    assert(file_has_write_root(files));
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(runtime);
    assert(runtime && ctx);
    JSGraphicContext graphics = {0};
    graphics.context = ctx;
    graphics.window = (Window *)1;
    graphics.video_files = &files;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys = JS_NewObject(ctx);
    js_video_register(ctx, sys, &graphics);
    JS_SetPropertyStr(ctx, global, "sys", sys);
    JS_FreeValue(ctx, global);
    test_frame_writer(&graphics, files);

    run(ctx,
        "globalThis.readerReady = null; globalThis.readerError = null;"
        "globalThis.readerFrames = []; globalThis.readerEof = 0;"
        "globalThis.readerId = sys.video.openReader('assets/background.mp4', {"
        " onReady(info) { readerReady = info; },"
        " onError(error) { readerError = error; } });"
        "if (!readerId) throw Error(sys.video.getError());");
    wait_for(&graphics, "readerReady || readerError");
    run(ctx,
        "if (readerError || readerReady.width !== 64 || readerReady.height !== 48"
        " || readerReady.frameRateNum / readerReady.frameRateDen !== 30"
        " || readerReady.durationUs !== 3000000"
        " || readerReady.sampleAspectRatioNum !== 1"
        " || readerReady.rotationDegrees !== 0"
        " || readerReady.colorPrimaries !== 'bt709'"
        " || !readerReady.exactFrameOrdinals || !readerReady.seekable)"
        " throw Error('single-source reader metadata is incorrect');"
        "function requestReaderFrame(ordinal) {"
        " if (!sys.video.requestFrame(readerId, ordinal, {"
        "  onFrame(frame, texture, ptsUs, durationUs, actualOrdinal, width, height) {"
        "   readerFrames.push({ frame, texture, ptsUs, durationUs, actualOrdinal, width, height });"
        "  }, onEof() { readerEof++; }, onError(error) { readerError = error; }"
        " })) throw Error(sys.video.getError());"
        "}"
        "requestReaderFrame(0);");
    wait_for(&graphics, "readerFrames.length === 1 || readerError");
    run(ctx,
        "if (readerError || readerFrames[0].actualOrdinal !== 0"
        " || readerFrames[0].ptsUs !== 0 || readerFrames[0].width !== 64"
        " || readerFrames[0].height !== 48) throw Error('first video frame is incorrect');"
        "const heldTexture = readerFrames[0].texture;"
        "requestReaderFrame(1);");
    wait_for(&graphics, "readerFrames.length === 2 || readerError");
    run(ctx,
        "if (readerError || readerFrames[1].actualOrdinal !== 1"
        " || readerFrames[1].ptsUs !== 33333 || readerFrames[1].texture === heldTexture)"
        " throw Error('sequential frame or retained texture is incorrect');"
        "requestReaderFrame(0);");
    wait_for(&graphics, "readerFrames.length === 3 || readerError");
    run(ctx,
        "if (readerError || readerFrames[2].ptsUs !== 0"
        " || readerFrames[0].texture !== heldTexture)"
        " throw Error('backward exact ordinal seek changed a retained texture');"
        "requestReaderFrame(89);");
    wait_for(&graphics, "readerFrames.length === 4 || readerError");
    run(ctx,
        "if (readerError || readerFrames[3].actualOrdinal !== 89"
        " || readerFrames[3].ptsUs !== 2966667)"
        " throw Error('last presentation-order frame is incorrect');"
        "requestReaderFrame(90);");
    wait_for(&graphics, "readerEof === 1 || readerError");
    run(ctx,
        "if (readerError || readerEof !== 1) throw Error('reader EOF was not explicit');");
    for (int ordinal = 2; ordinal < 6; ++ordinal)
    {
        char script[128];
        snprintf(script, sizeof(script), "requestReaderFrame(%d);", ordinal);
        run(ctx, script);
        char expression[128];
        snprintf(expression, sizeof(expression),
                 "readerFrames.length === %d || readerError", ordinal + 3);
        wait_for(&graphics, expression);
    }
    run(ctx,
        "if (readerError) throw Error(readerError.message);"
        "if (sys.video.requestFrame(readerId, 8, {"
        " onFrame() {}, onEof() {}, onError() {} })"
        " || !sys.video.getError().includes('capacity'))"
        " throw Error('retained-frame backpressure was not reported');"
        "if (!sys.video.closeReader(readerId)) throw Error(sys.video.getError());");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 8);
    run(ctx,
        "for (const frame of readerFrames)"
        " if (!sys.video.releaseFrame(frame.frame)) throw Error(sys.video.getError());");
    assert(live_textures == 0);

    run(ctx,
        "globalThis.independentReady = 0; globalThis.independentFrames = [];"
        "globalThis.independentErrors = [];"
        "const independentCallbacks = {"
        " onReady() { independentReady++; },"
        " onError(error) { independentErrors.push(error); }"
        "};"
        "globalThis.readerA = sys.video.openReader('assets/background.mp4', independentCallbacks);"
        "globalThis.readerB = sys.video.openReader('assets/inset.mp4', independentCallbacks);"
        "if (!readerA || !readerB) throw Error(sys.video.getError());");
    wait_for(&graphics, "independentReady === 2 || independentErrors.length");
    run(ctx,
        "if (independentErrors.length) throw Error(independentErrors[0].message);"
        "for (const [reader, ordinal] of [[readerA, 3], [readerB, 10]]) {"
        " if (!sys.video.requestFrame(reader, ordinal, {"
        "  onFrame(frame, texture, ptsUs, durationUs, actualOrdinal) {"
        "   independentFrames.push({ frame, reader, actualOrdinal });"
        "  }, onEof() { throw Error('unexpected EOF'); },"
        "  onError(error) { independentErrors.push(error); }"
        " })) throw Error(sys.video.getError());"
        "}");
    wait_for(&graphics, "independentFrames.length === 2 || independentErrors.length");
    run(ctx,
        "if (independentErrors.length"
        " || independentFrames.find(frame => frame.reader === readerA).actualOrdinal !== 3"
        " || independentFrames.find(frame => frame.reader === readerB).actualOrdinal !== 10)"
        " throw Error('independent readers shared seek state');"
        "for (const frame of independentFrames)"
        " if (!sys.video.releaseFrame(frame.frame)) throw Error(sys.video.getError());"
        "if (!sys.video.closeReader(readerA) || !sys.video.closeReader(readerB))"
        " throw Error(sys.video.getError());");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 0);

    run(ctx,
        "globalThis.rejectedReady = false; globalThis.rejectedError = null;"
        "const rejectedReader = sys.video.openReader('assets/not-30-fps.mp4', {"
        " onReady() { rejectedReady = true; },"
        " onError(error) { rejectedError = error; } });"
        "if (!rejectedReader) throw Error(sys.video.getError());"
        "globalThis.rejectedReader = rejectedReader;");
    wait_for(&graphics, "rejectedError || rejectedReady");
    run(ctx,
        "if (!rejectedError || rejectedReady || !rejectedError.message)"
        " throw Error('unsupported frame rate was not reported');"
        "if (!sys.video.closeReader(rejectedReader)) throw Error(sys.video.getError());");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics));

    run(ctx,
        "globalThis.ordinalReady = false; globalThis.ordinalError = null;"
        "globalThis.ordinalFrames = [];"
        "const ordinalReader = sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady(info) {"
        "  if (info.durationUs !== 2000000) throw Error('wrong B-frame fixture duration');"
        "  ordinalReady = true;"
        " }, onError(error) { ordinalError = error; }"
        "});"
        "if (!ordinalReader) throw Error(sys.video.getError());"
        "globalThis.ordinalReader = ordinalReader;"
        "function requestOrdinalFrame(ordinal) {"
        " if (!sys.video.requestFrame(ordinalReader, ordinal, {"
        "  onFrame(frame, texture, ptsUs, durationUs, actualOrdinal) {"
        "   ordinalFrames.push({ frame, texture, ptsUs, durationUs, actualOrdinal });"
        "  }, onEof() { throw Error('unexpected ordinal fixture EOF'); },"
        "  onError(error) { ordinalError = error; }"
        " })) throw Error(sys.video.getError());"
        "}");
    wait_for(&graphics, "ordinalReady || ordinalError");
    run(ctx,
        "if (ordinalError) throw Error(ordinalError.message);");
    uint64_t sequential_hashes[60];
    for (int ordinal = 0; ordinal < 60; ++ordinal)
    {
        char request[128];
        snprintf(request, sizeof(request), "requestOrdinalFrame(%d);", ordinal);
        run(ctx, request);
        char expression[128];
        snprintf(expression, sizeof(expression),
                 "ordinalFrames.length === %d || ordinalError", ordinal + 1);
        wait_for(&graphics, expression);
        char script[512];
        snprintf(script, sizeof(script),
                 "if (ordinalError || ordinalFrames[%d].actualOrdinal !== %d"
                 " || ordinalFrames[%d].ptsUs !== Math.round(%d * 1000000 / 30))"
                 " throw Error('sequential frame ordinal or timestamp mismatch');"
                 "if (!sys.video.releaseFrame(ordinalFrames[%d].frame))"
                 " throw Error(sys.video.getError());",
                 ordinal, ordinal, ordinal, ordinal, ordinal);
        run(ctx, script);
        sequential_hashes[ordinal] = frame_texture_hash(ctx, ordinal);
    }
    size_t unique_frames = 0;
    for (size_t i = 0; i < sizeof(sequential_hashes) /
                                sizeof(sequential_hashes[0]); ++i)
    {
        bool seen = false;
        for (size_t j = 0; j < i; ++j)
            if (sequential_hashes[i] == sequential_hashes[j])
            {
                seen = true;
                break;
            }
        if (!seen)
            unique_frames++;
    }
    assert(unique_frames >= 30);

    const int seek_ordinals[] = {59, 0, 30, 2, 58, 29, 1};
    for (size_t i = 0; i < sizeof(seek_ordinals) / sizeof(seek_ordinals[0]); ++i)
    {
        int ordinal = seek_ordinals[i];
        int frame_index = 60 + (int)i;
        char script[512];
        snprintf(script, sizeof(script), "requestOrdinalFrame(%d);", ordinal);
        run(ctx, script);
        char expression[128];
        snprintf(expression, sizeof(expression),
                 "ordinalFrames.length === %d || ordinalError", frame_index + 1);
        wait_for(&graphics, expression);
        snprintf(script, sizeof(script),
                 "if (ordinalError || ordinalFrames[%d].actualOrdinal !== %d"
                 " || ordinalFrames[%d].ptsUs !== Math.round(%d * 1000000 / 30))"
                 " throw Error('random frame ordinal or timestamp mismatch');"
                 "if (!sys.video.releaseFrame(ordinalFrames[%d].frame))"
                 " throw Error(sys.video.getError());",
                 frame_index, ordinal, frame_index, ordinal, frame_index);
        run(ctx, script);
        assert(frame_texture_hash(ctx, frame_index) ==
               sequential_hashes[ordinal]);
    }
    run(ctx, "requestOrdinalFrame(10);");
    wait_for(&graphics, "ordinalFrames.length === 68 || ordinalError");
    run(ctx,
        "if (ordinalError) throw Error(ordinalError.message);"
        "if (!sys.video.releaseFrame(ordinalFrames[67].frame))"
        " throw Error(sys.video.getError());"
        "globalThis.ordinalEof = false;"
        "if (!sys.video.requestFrame(ordinalReader, 100, {"
        " onFrame() { throw Error('unexpected frame beyond EOF'); },"
        " onEof() { ordinalEof = true; },"
        " onError(error) { ordinalError = error; }"
        "})) throw Error(sys.video.getError());");
    wait_for(&graphics, "ordinalEof || ordinalError");
    run(ctx,
        "if (ordinalError) throw Error(ordinalError.message);"
        "requestOrdinalFrame(11);");
    wait_for(&graphics, "ordinalFrames.length === 69 || ordinalError");
    run(ctx,
        "if (ordinalError || ordinalFrames[68].actualOrdinal !== 11)"
        " throw Error('request after EOF did not recover');"
        "if (!sys.video.releaseFrame(ordinalFrames[68].frame))"
        " throw Error(sys.video.getError());");
    assert(frame_texture_hash(ctx, 68) == sequential_hashes[11]);
    run(ctx, "if (!sys.video.closeReader(ordinalReader))"
             " throw Error(sys.video.getError());");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 0);

    run(ctx,
        "globalThis.cancelReady = false; globalThis.cancelCallbacks = 0;"
        "globalThis.cancelError = null;"
        "const cancelReader = sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady() { cancelReady = true; },"
        " onError() { cancelCallbacks++; }"
        "});"
        "if (!cancelReader) throw Error(sys.video.getError());"
        "globalThis.cancelReader = cancelReader;"
        "function requestThenCloseReader() {"
        " if (!sys.video.requestFrame(cancelReader, 59, {"
        "  onFrame() { cancelCallbacks++; }, onEof() { cancelCallbacks++; },"
        "  onError(error) { cancelCallbacks++; cancelError = error; }"
        " })) throw Error(sys.video.getError());"
        " if (!sys.video.closeReader(cancelReader)) throw Error(sys.video.getError());"
        " if (sys.video.closeReader(cancelReader)) throw Error('reader close was not idempotent');"
        "}");
    wait_for(&graphics, "cancelReady");
    run(ctx, "requestThenCloseReader();");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 0);
    run(ctx, "if (cancelCallbacks !== 1 || cancelError.code !== 'video.cancelled')"
             " throw Error('closed reader did not cancel its pending request');");

    run(ctx,
        "globalThis.explicitReady = false; globalThis.explicitOutcomes = [];"
        "globalThis.explicitReader = sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady() { explicitReady = true; }, onError(error) { throw Error(error.message); }"
        "});"
        "if (!explicitReader) throw Error(sys.video.getError());");
    wait_for(&graphics, "explicitReady");
    run(ctx,
        "if (!sys.video.requestFrame(explicitReader, 59, {"
        " onFrame() { explicitOutcomes.push('frame'); },"
        " onEof() { explicitOutcomes.push('eof'); },"
        " onError(error) { explicitOutcomes.push(error.code); }"
        "})) throw Error(sys.video.getError());"
        "if (!sys.video.cancelFrameRequest(explicitReader)) throw Error(sys.video.getError());"
        "if (sys.video.cancelFrameRequest(explicitReader)) throw Error('cancel accepted twice');");
    wait_for(&graphics, "explicitOutcomes.length");
    run(ctx,
        "if (explicitOutcomes.join() !== 'video.cancelled')"
        " throw Error('cancelled request had the wrong outcome');"
        "if (!sys.video.requestFrame(explicitReader, 0, {"
        " onFrame(frame, texture, pts, duration, ordinal) {"
        "  if (ordinal !== 0 || !sys.video.releaseFrame(frame)) throw Error('invalid frame');"
        "  explicitOutcomes.push('recovered');"
        " }, onEof() { throw Error('unexpected EOF'); },"
        " onError(error) { throw Error(error.message); }"
        "})) throw Error(sys.video.getError());");
    wait_for(&graphics, "explicitOutcomes.length === 2");
    run(ctx,
        "if (explicitOutcomes[1] !== 'recovered') throw Error('cancel recovery failed');"
        "if (!sys.video.requestFrame(explicitReader, 1, {"
        " onFrame() { explicitOutcomes.push('stale'); },"
        " onEof() { explicitOutcomes.push('eof'); },"
        " onError(error) { explicitOutcomes.push(error.code); }"
        "})) throw Error(sys.video.getError());");
    const struct timespec completion_delay = {0, 100000000};
    nanosleep(&completion_delay, NULL);
    run(ctx, "if (!sys.video.cancelFrameRequest(explicitReader))"
             " throw Error(sys.video.getError());");
    wait_for(&graphics, "explicitOutcomes.length === 3");
    run(ctx, "if (explicitOutcomes[2] !== 'video.cancelled' || !sys.video.closeReader(explicitReader))"
             " throw Error('reader failed to recover after cancellation');");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 0);

    run(ctx,
        "globalThis.openCancel = []; globalThis.openingReader ="
        " sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady() { openCancel.push('ready'); },"
        " onError(error) { openCancel.push(error.code); }"
        "});"
        "if (!openingReader || !sys.video.closeReader(openingReader))"
        " throw Error(sys.video.getError());");
    wait_for(&graphics, "openCancel.length");
    run(ctx, "if (openCancel.join() !== 'video.cancelled')"
             " throw Error('closing an opening reader did not cancel its open');");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics));

    run(ctx,
        "globalThis.lossReady = false; globalThis.lossFrame = 0;"
        "globalThis.lossOutcomes = [];"
        "globalThis.lossReader = sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady() { lossReady = true; }, onError(error) { throw Error(error.message); }"
        "});"
        "if (!lossReader) throw Error(sys.video.getError());");
    wait_for(&graphics, "lossReady");
    run(ctx,
        "if (!sys.video.requestFrame(lossReader, 0, {"
        " onFrame(frame) { lossFrame = frame; },"
        " onEof() { throw Error('unexpected EOF'); },"
        " onError(error) { throw Error(error.message); }"
        "})) throw Error(sys.video.getError());");
    wait_for(&graphics, "lossFrame");
    assert(live_textures == 1);
    run(ctx,
        "if (!sys.video.requestFrame(lossReader, 59, {"
        " onFrame() { lossOutcomes.push('frame'); },"
        " onEof() { lossOutcomes.push('eof'); },"
        " onError(error) { lossOutcomes.push(error.code); }"
        "})) throw Error(sys.video.getError());");
    js_video_context_lost(&graphics);
    live_textures = 0;
    run(ctx,
        "if (sys.video.releaseFrame(lossFrame)) throw Error('lost frame remained valid');"
        "if (sys.video.requestFrame(lossReader, 1, {"
        " onFrame() {}, onEof() {}, onError() {} }))"
        " throw Error('accepted request during context loss');");
    js_video_context_restored(&graphics);
    wait_for(&graphics, "lossOutcomes.length");
    run(ctx,
        "if (lossOutcomes.join() !== 'video.graphics_context_lost')"
        " throw Error('context loss did not invalidate the pending frame');"
        "if (!sys.video.requestFrame(lossReader, 1, {"
        " onFrame(frame) { if (!sys.video.releaseFrame(frame)) throw Error(sys.video.getError());"
        "  lossOutcomes.push('recovered'); },"
        " onEof() { throw Error('unexpected EOF'); },"
        " onError(error) { throw Error(error.message); }"
        "})) throw Error(sys.video.getError());");
    wait_for(&graphics, "lossOutcomes.length === 2");
    run(ctx, "if (lossOutcomes[1] !== 'recovered' || !sys.video.closeReader(lossReader))"
             " throw Error('reader failed to recover after context restoration');");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 0);

    run(ctx,
        "globalThis.frames = []; globalThis.ready = []; globalThis.ends = 0;"
        "globalThis.errors = []; globalThis.id = sys.video.preview("
        "'assets/background.mp4', 'assets/inset.mp4', {"
        " onReady(d) { ready.push(d); },"
        " onFrame(t, p) { frames.push([t, p]); },"
        " onEnd() { ends++; }, onError(e) { errors.push(e); } });"
        "if (!id) throw Error(sys.video.getError());");
    wait_for(&graphics, "frames.length === 1");
    run(ctx,
        "if (ready.length !== 1 || ready[0] !== 2000000 || frames[0][1] !== 0)"
        " throw Error('incorrect initial duration or frame');"
        "for (const value of [-1, 0.1, NaN, Infinity, '1', 9007199254740992]) {"
        " let threw = false; try { sys.video.seekPreview(id, value); } catch (e) { threw = true; }"
        " if (!threw) throw Error('accepted invalid timestamp'); }"
        "if (!sys.video.pausePreview(id)) throw Error(sys.video.getError());"
        "if (!sys.video.seekPreview(id, 1500000) || !sys.video.seekPreview(id, 500000))"
        " throw Error(sys.video.getError());");
    wait_for(&graphics, "frames.length === 2");
    run(ctx,
        "if (frames[1][1] !== 500000 || ready.length !== 1)"
        " throw Error('seek was not coalesced or ready repeated');"
        "if (!sys.video.seekPreview(id, 33333)) throw Error(sys.video.getError());");
    wait_for(&graphics, "frames.length === 3");
    run(ctx,
        "if (frames[2][1] !== 33333) throw Error('seek missed frame boundary');"
        "if (!sys.video.seekPreview(id, 33332)) throw Error(sys.video.getError());");
    wait_for(&graphics, "frames.length === 4");
    run(ctx, "if (frames[3][1] !== 0) throw Error('seek passed requested time');");
    now += 1.0;
    for (int i = 0; i < 10; ++i)
        js_video_poll(&graphics);
    run(ctx, "if (frames.length !== 4) throw Error('paused seek advanced');"
             "if (!sys.video.resumePreview(id)) throw Error(sys.video.getError());");
    now += 10.0;
    wait_for(&graphics, "ends === 1 || errors.length > 0");
    run(ctx,
        "if (errors.length || ends !== 1) throw Error('preview did not end once');"
        "if (!sys.video.seekPreview(id, 2000000)) throw Error(sys.video.getError());");
    wait_for(&graphics, "frames[frames.length - 1][1] === 1966667");
    run(ctx,
        "if (!sys.video.seekPreview(id, 0) || !sys.video.closePreview(id))"
        " throw Error('close during seek failed');"
        "if (sys.video.closePreview(id)) throw Error('closed handle remained valid');");
    
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    assert(!js_video_pending(&graphics) && live_textures == 0);

    run(ctx,
        "globalThis.reentrantFrames = []; globalThis.reentrantReady = 0;"
        "id = sys.video.preview('assets/background.mp4', 'assets/inset.mp4', {"
        " onReady(d) { reentrantReady++; if (!sys.video.seekPreview(id, 1000000))"
        " throw Error(sys.video.getError()); },"
        " onFrame(t,p) { reentrantFrames.push(p); },"
        " onEnd() {}, onError(e) { throw Error(e.message); } });"
        "if (!id) throw Error(sys.video.getError());");
    wait_for(&graphics, "reentrantFrames.length > 0");
    run(ctx, "if (reentrantReady !== 1 || reentrantFrames[0] !== 1000000)"
             " throw Error('stale frame delivered after reentrant seek');");
    run(ctx, "globalThis.previewLoss = []; if (!sys.video.closePreview(id))"
             " throw Error(sys.video.getError());");
    for (int i = 0; i < 10000 && js_video_pending(&graphics); ++i)
    {
        js_video_poll(&graphics);
        const struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    run(ctx,
        "id = sys.video.preview('assets/background.mp4', 'assets/inset.mp4', {"
        " onFrame() { previewLoss.push('frame'); }, onEnd() {},"
        " onError(error) { previewLoss.push(error.code); } });"
        "if (!id) throw Error(sys.video.getError());");
    wait_for(&graphics, "previewLoss.length");
    js_video_context_lost(&graphics);
    live_textures = 0;
    js_video_context_restored(&graphics);
    wait_for(&graphics, "previewLoss.length === 2");
    run(ctx,
        "if (previewLoss[1] !== 'video.graphics_context_lost')"
        " throw Error('preview did not report context loss');"
        "if (sys.video.seekPreview(id, 0)) throw Error('lost preview was reused');"
        "if (!sys.video.closePreview(id)) throw Error(sys.video.getError());");
    JSContext *peer_ctx = JS_NewContext(runtime);
    assert(peer_ctx);
    JSGraphicContext peer = {0};
    peer.context = peer_ctx;
    peer.window = (Window *)1;
    peer.video_files = &files;
    JSValue peer_global = JS_GetGlobalObject(peer_ctx);
    JSValue peer_sys = JS_NewObject(peer_ctx);
    js_video_register(peer_ctx, peer_sys, &peer);
    JS_SetPropertyStr(peer_ctx, peer_global, "sys", peer_sys);
    JS_FreeValue(peer_ctx, peer_global);
    run(peer_ctx,
        "globalThis.peerReady = false; globalThis.peerFrame = false;"
        "globalThis.peerReader = sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady() { peerReady = true; }, onError(error) { throw Error(error.message); }"
        "}); if (!peerReader) throw Error(sys.video.getError());");
    wait_for(&peer, "peerReady");
    run(ctx,
        "globalThis.shutdownReady = false;"
        "globalThis.shutdownReader = sys.video.openReader('assets/ordinal-bframes.mp4', {"
        " onReady() { shutdownReady = true; }, onError(error) { throw Error(error.message); }"
        "}); if (!shutdownReader) throw Error(sys.video.getError());");
    wait_for(&graphics, "shutdownReady");
    run(ctx,
        "if (!sys.video.requestFrame(shutdownReader, 59, {"
        " onFrame() { throw Error('callback after shutdown'); },"
        " onEof() { throw Error('callback after shutdown'); },"
        " onError() { throw Error('callback after shutdown'); }"
        "})) throw Error(sys.video.getError());");
    run(ctx,
        "const shutdownWriter=sys.video.openFrameWriter('files/shutdown.mp4',writerOptions,{"
        " onReady(){throw Error('writer callback after shutdown');},"
        " onComplete(){throw Error('writer completed after shutdown');},"
        " onError(){throw Error('writer error after shutdown');}"
        "}); if(!shutdownWriter) throw Error(sys.video.getError());");
    js_video_cleanup(&graphics);
    assert(live_textures == 0);
    JS_FreeContext(ctx);
    run(peer_ctx,
        "if (!sys.video.requestFrame(peerReader, 1, {"
        " onFrame(frame) { if (!sys.video.releaseFrame(frame)) throw Error(sys.video.getError());"
        "  peerFrame = true; }, onEof() { throw Error('unexpected EOF'); },"
        " onError(error) { throw Error(error.message); }"
        "})) throw Error(sys.video.getError());");
    wait_for(&peer, "peerFrame");
    run(peer_ctx, "if (!sys.video.closeReader(peerReader)) throw Error(sys.video.getError());");
    js_video_cleanup(&peer);
    assert(live_textures == 0);
    JS_FreeContext(peer_ctx);
    JS_FreeRuntime(runtime);
    file_destroy(files);
    return 0;
}