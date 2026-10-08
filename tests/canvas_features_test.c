#include "graphics/js_canvas_bindings.h"
#include "graphics/lua_canvas_bindings.h"
#include "graphics/skia_wrapper.h"
#include "graphics/wasm_canvas_bindings.h"
#include "device/device_service.h"
#include "device/js_device_bindings.h"
#include "accessibility/accessibility_service.h"
#include "accessibility/js_accessibility_bindings.h"
#include "accessibility/lua_accessibility_bindings.h"

#include "lauxlib.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 64

typedef struct
{
    SkiaSurface *surface;
    SkiaCanvas *canvas;
    uint8_t pixels[SIZE * SIZE * 4];
} Raster;

static void raster_init(Raster *raster)
{
    raster->surface = skia_surface_create_raster(SIZE, SIZE);
    assert(raster->surface);
    raster->canvas = skia_surface_get_canvas(raster->surface);
    skia_canvas_clear(raster->canvas, SKIA_COLOR_WHITE);
}

static const uint8_t *pixel(Raster *raster, int x, int y)
{
    assert(skia_surface_read_pixels(raster->surface, raster->pixels));
    return raster->pixels + (y * SIZE + x) * 4;
}

static void expect_pixel(Raster *raster, int x, int y, int r, int g, int b, int tolerance, const char *what)
{
    const uint8_t *p = pixel(raster, x, y);
    if (abs(p[0] - r) > tolerance || abs(p[1] - g) > tolerance || abs(p[2] - b) > tolerance)
    {
        fprintf(stderr, "%s: pixel (%d, %d) is (%d, %d, %d), expected (%d, %d, %d)\n",
                what, x, y, p[0], p[1], p[2], r, g, b);
        assert(false);
    }
}

static bool is_white(Raster *raster, int x, int y)
{
    const uint8_t *p = pixel(raster, x, y);
    return p[0] > 250 && p[1] > 250 && p[2] > 250;
}

static int ink_pixels(Raster *raster, int x0, int y0, int x1, int y1)
{
    int count = 0;
    assert(skia_surface_read_pixels(raster->surface, raster->pixels));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            const uint8_t *p = raster->pixels + (y * SIZE + x) * 4;
            count += p[0] < 200 || p[1] < 200 || p[2] < 200;
        }
    return count;
}

static void test_wrapper(void)
{
    Raster raster;
    SkiaPaint *paint = skia_paint_create();
    const uint32_t red_blue[] = {0xFFFF0000u, 0xFF0000FFu};

    raster_init(&raster);
    assert(skia_paint_set_linear_gradient(paint, 0, 0, SIZE, 0, red_blue, NULL, 2));
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE, SIZE, paint);
    expect_pixel(&raster, 0, 10, 255, 0, 0, 24, "linear start");
    expect_pixel(&raster, SIZE - 1, 10, 0, 0, 255, 24, "linear end");
    const uint8_t *middle = pixel(&raster, SIZE / 2, 10);
    assert(middle[0] > 40 && middle[2] > 40);
    skia_paint_set_color(paint, 0xFF00FF00u);
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE, SIZE, paint);
    expect_pixel(&raster, 0, 10, 0, 255, 0, 0, "color clears gradient");

    assert(!skia_paint_set_linear_gradient(paint, 0, 0, 1, 0, red_blue, NULL, 1));
    assert(!skia_paint_set_radial_gradient(paint, 0, 0, 0, red_blue, NULL, 2));
    assert(!skia_paint_set_sweep_gradient(paint, 0, 0, NULL, NULL, 2));

    const float stops[] = {0.0f, 1.0f};
    assert(skia_paint_set_radial_gradient(paint, 32, 32, 32, red_blue, stops, 2));
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE, SIZE, paint);
    expect_pixel(&raster, 32, 32, 255, 0, 0, 32, "radial center");
    expect_pixel(&raster, 0, 0, 0, 0, 255, 24, "radial outside");
    assert(skia_paint_set_sweep_gradient(paint, 32, 32, red_blue, NULL, 2));
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE, SIZE, paint);
    expect_pixel(&raster, 60, 33, 255, 0, 0, 24, "sweep start");
    skia_paint_clear_shader(paint);
    skia_paint_destroy(paint);

    paint = skia_paint_create();
    skia_paint_set_color(paint, SKIA_COLOR_BLACK);
    raster_init(&raster);
    skia_canvas_save(raster.canvas);
    skia_canvas_clip_round_rect(raster.canvas, 0, 0, SIZE, SIZE, 20, 20);
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE, SIZE, paint);
    skia_canvas_restore(raster.canvas);
    assert(is_white(&raster, 1, 1));
    expect_pixel(&raster, 32, 32, 0, 0, 0, 0, "round clip center");
    expect_pixel(&raster, 32, 1, 0, 0, 0, 0, "round clip edge");

    raster_init(&raster);
    SkiaPath *path = skia_path_create();
    skia_path_move_to(path, 0, 0);
    skia_path_line_to(path, 0, SIZE);
    skia_path_line_to(path, SIZE, SIZE);
    skia_path_close(path);
    skia_canvas_save(raster.canvas);
    skia_canvas_clip_path(raster.canvas, path);
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE, SIZE, paint);
    skia_canvas_restore(raster.canvas);
    skia_path_destroy(path);
    assert(is_white(&raster, 50, 10));
    expect_pixel(&raster, 10, 50, 0, 0, 0, 0, "path clip inside");

    raster_init(&raster);
    skia_paint_set_color(paint, 0xFFFF0000u);
    skia_canvas_save_layer(raster.canvas, NULL, 128, 0.0f);
    skia_canvas_draw_rect(raster.canvas, 0, 0, 40, 40, paint);
    skia_canvas_draw_rect(raster.canvas, 20, 20, 40, 40, paint);
    skia_canvas_restore(raster.canvas);
    expect_pixel(&raster, 10, 10, 255, 127, 127, 3, "layer alone");
    expect_pixel(&raster, 30, 30, 255, 127, 127, 3, "layer overlap");

    raster_init(&raster);
    skia_paint_set_color(paint, SKIA_COLOR_BLACK);
    skia_canvas_draw_rect(raster.canvas, 0, 0, SIZE / 2, SIZE, paint);
    SkiaRect bounds = {16, 16, 48, 48};
    int depth = skia_canvas_get_save_count(raster.canvas);
    skia_canvas_save_layer(raster.canvas, &bounds, 255, 6.0f);
    skia_canvas_restore(raster.canvas);
    assert(skia_canvas_get_save_count(raster.canvas) == depth);
    const uint8_t *edge = pixel(&raster, SIZE / 2, 32);
    assert(edge[0] > 30 && edge[0] < 225);
    expect_pixel(&raster, SIZE / 2, 2, 255, 255, 255, 0, "backdrop stays inside bounds");

    SkiaParagraphMetrics one = skia_measure_paragraph("one two three four five six", 0, 10, 0, 0, NULL);
    assert(one.lines == 1 && one.width > 0);
    assert(fabsf(one.height - 10 * SKIA_DEFAULT_LINE_HEIGHT) < 0.01f);
    SkiaParagraphMetrics wrapped = skia_measure_paragraph("one two three four five six", one.width / 2, 10, 1.5f, 0, NULL);
    assert(wrapped.lines >= 2 && wrapped.width <= one.width / 2 + 0.01f);
    assert(fabsf(wrapped.height - wrapped.lines * 15.0f) < 0.01f);
    SkiaParagraphMetrics capped = skia_measure_paragraph("one two three four five six", one.width / 3, 10, 0, 2, NULL);
    assert(capped.lines == 2 && capped.width <= one.width / 3 + 0.01f);
    SkiaParagraphMetrics hard = skia_measure_paragraph("a\nb\n\nc", 0, 10, 1, 0, NULL);
    assert(hard.lines == 4);
    SkiaParagraphMetrics word = skia_measure_paragraph("abcdefghijklmnopqrstuvwxyz", 30, 10, 1, 0, NULL);
    assert(word.lines > 1 && word.width <= 30.01f);
    SkiaParagraphMetrics empty = skia_measure_paragraph("", 30, 10, 1, 0, NULL);
    assert(empty.lines == 0 && empty.height == 0);
    SkiaParagraphMetrics utf8 = skia_measure_paragraph("caf\xC3\xA9 na\xC3\xAFve \xE2\x9C\x93", 20, 10, 1, 0, NULL);
    assert(utf8.lines >= 2);

    raster_init(&raster);
    SkiaParagraphMetrics drawn = skia_canvas_draw_paragraph(raster.canvas, "Wrapped text here", 2, 2, 40, 12,
                                                            0, SKIA_TEXT_ALIGN_CENTER, 0, paint, NULL);
    assert(drawn.lines >= 2);
    assert(ink_pixels(&raster, 0, 0, SIZE, (int)drawn.height + 4) > 20);
    assert(ink_pixels(&raster, 0, (int)drawn.height + 6, SIZE, SIZE) == 0);

    SkiaTextSpan mixed[] = {{"small ", 10, NULL, 0, false}, {"BIG", 30, NULL, 0xFFFF0000u, true}};
    SkiaParagraphMetrics rich = skia_measure_rich_text(mixed, 2, 0, 1, 0);
    assert(rich.lines == 1 && fabsf(rich.height - 30) < 0.01f);
    assert(rich.width > skia_measure_text_width("small ", 10, NULL));
    SkiaTextSpan wrapped_spans[] = {{"one two three ", 10, NULL, 0, false}, {"four five six", 20, NULL, 0, false}};
    SkiaParagraphMetrics rich_wrapped = skia_measure_rich_text(wrapped_spans, 2, 60, 1, 0);
    assert(rich_wrapped.lines >= 3 && rich_wrapped.height > 10 * rich_wrapped.lines);
    raster_init(&raster);
    skia_paint_set_color(paint, SKIA_COLOR_BLACK);
    SkiaTextSpan red[] = {{"\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88", 40, NULL, 0xFFFF0000u, true}};
    skia_canvas_draw_rich_text(raster.canvas, red, 1, 0, 0, 0, 1, SKIA_TEXT_ALIGN_LEFT, 0, paint);
    int reds = 0;
    assert(skia_surface_read_pixels(raster.surface, raster.pixels));
    for (int i = 0; i < SIZE * SIZE; i++)
        reds += raster.pixels[i * 4] > 200 && raster.pixels[i * 4 + 1] < 60;
    assert(reds > 20 && ink_pixels(&raster, 0, 0, SIZE, SIZE) >= reds);

    skia_paint_destroy(paint);
    skia_surface_destroy(raster.surface);
}

static void js_eval(JSRuntimeContext *runtime, const char *script)
{
    JSValue result = JS_Eval(runtime->context, script, strlen(script), "canvas_features.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(runtime->context);
        const char *message = JS_ToCString(runtime->context, exception);
        fprintf(stderr, "JavaScript canvas features failure: %s\n", message ? message : "exception");
        JS_FreeCString(runtime->context, message);
        JS_FreeValue(runtime->context, exception);
        assert(false);
    }
    JS_FreeValue(runtime->context, result);
}

static void test_javascript(const char *root)
{
    Raster raster;
    raster_init(&raster);
    JSRuntimeContext *runtime = js_runtime_create(root);
    JSGraphicContext *graphic = js_graphic_init(runtime);
    assert(runtime && graphic);
    js_graphic_set_frame_context(graphic, raster.canvas, NULL, NULL, SIZE, SIZE, 1.0f);
    js_eval(runtime,
            "const c = sys.canvas;\n"
            "function throws(fn, type) { try { fn(); } catch (e) { if (e instanceof type) return; throw e; }"
            "  throw Error('expected ' + type.name); }\n"
            "if (c.setGradient('linear', 0, 0, 64, 0, ['#FF0000', 0xFF0000FF]) !== true) throw Error('linear');\n"
            "c.drawRect(0, 0, 64, 16);\n"
            "if (!c.setGradient('radial', 32, 32, 10, ['#FFF', '#000'], [0, 1])) throw Error('radial');\n"
            "if (!c.setGradient('sweep', 32, 32, ['#F00', '#0F0', '#F00'])) throw Error('sweep');\n"
            "if (c.setGradient(null) !== true || c.setGradient('none') !== true) throw Error('clear');\n"
            "throws(() => c.setGradient('conic', 0, 0, ['#000', '#fff']), RangeError);\n"
            "throws(() => c.setGradient('linear', 0, 0, 1, 1, ['#000']), TypeError);\n"
            "throws(() => c.setGradient('linear', 0, 0, 1, 1, ['#000', '#fff'], [0]), TypeError);\n"
            "c.setFillColor('#000000');\n"
            "c.save(); c.clipRoundRect(0, 16, 64, 16, 8); c.drawRect(0, 16, 64, 16); c.restore();\n"
            "const p = sys.path.create(); sys.path.addRect(p, 0, 32, 8, 8);\n"
            "c.save(); c.clipPath(p); c.drawRect(0, 32, 64, 16); c.restore();\n"
            "throws(() => c.clipPath(99), RangeError);\n"
            "c.setFillColor('#FF0000');\n"
            "c.saveLayer(128); c.drawRect(40, 32, 20, 10); c.drawRect(40, 32, 20, 10); c.restore();\n"
            "c.saveLayer(255, 60, 0, 4, 4, 2); c.restore();\n"
            "c.setFillColor('#000000');\n"
            "const m = c.measureParagraph('one two three four', 30, 10, { lineHeight: 1 });\n"
            "if (m.lines < 2 || m.height !== m.lines * 10 || m.width > 30) throw Error('measure ' + JSON.stringify(m));\n"
            "const d = c.drawParagraph('one two three four', 0, 48, 64, 8, { align: 'right', maxLines: 1 });\n"
            "if (d.lines !== 1) throw Error('draw ' + JSON.stringify(d));\n"
            "throws(() => c.measureParagraph('x', 10, 10, { align: 'middle' }), RangeError);\n"
            "throws(() => c.drawParagraph(5, 0, 0, 10), TypeError);\n"
            "const r = c.measureRichText(['plain ', { text: 'big', size: 24, color: '#F00' }], 0, { size: 8, lineHeight: 1 });\n"
            "if (r.lines !== 1 || r.height !== 24) throw Error('rich ' + JSON.stringify(r));\n"
            "throws(() => c.measureRichText([{ size: 3 }], 10), TypeError);\n"
            "throws(() => c.measureRichText([{ text: 'x', font: 'nope' }], 10), ReferenceError);\n"
            "c.drawRichText(['a ', { text: 'b', color: 0xFF00FF00 }], 0, 0, 64, { align: 'center' });\n"
            "const svg = sys.path.create();\n"
            "if (sys.path.addSvg(svg, 'M20 40h8v6h-8z') !== true) throw Error('addSvg');\n"
            "if (sys.path.addSvg(svg, 'M1 2 bogus') !== false || sys.path.addSvg(99, 'M0 0') !== false) throw Error('addSvg false');\n"
            "c.setFillColor('#000000'); c.drawPath(svg);\n"
            "if (c.setFont(null) !== true) throw Error('setFont(null)');\n");
    expect_pixel(&raster, 24, 43, 0, 0, 0, 0, "js svg path");
    assert(is_white(&raster, 31, 43));
    expect_pixel(&raster, 0, 8, 255, 0, 0, 24, "js linear");
    expect_pixel(&raster, SIZE - 1, 8, 0, 0, 255, 24, "js linear end");
    assert(is_white(&raster, 0, 16));
    expect_pixel(&raster, 32, 24, 0, 0, 0, 0, "js round clip");
    expect_pixel(&raster, 4, 36, 0, 0, 0, 0, "js path clip");
    assert(is_white(&raster, 20, 36));
    expect_pixel(&raster, 50, 36, 255, 127, 127, 3, "js layer");
    assert(ink_pixels(&raster, 0, 48, SIZE, SIZE) > 5);

    skia_canvas_clear(raster.canvas, SKIA_COLOR_WHITE);
    js_eval(runtime,
            "c.setFillColor('#000000'); c.save(); c.translate(64, 0); c.rotate(90);\n"
            "c.drawRect(0, 0, 8, 8); c.restore();\n"
            "c.save(); c.rotate(180, 32, 32); c.drawRect(0, 56, 8, 8); c.restore();\n");
    expect_pixel(&raster, 60, 4, 0, 0, 0, 0, "js rotate");
    expect_pixel(&raster, 60, 4, 0, 0, 0, 0, "js rotate around");
    assert(is_white(&raster, 4, 4));
    js_graphic_destroy(graphic);
    js_runtime_destroy(runtime);
    skia_surface_destroy(raster.surface);
}

static void test_lua(const char *root)
{
    Raster raster;
    raster_init(&raster);
    LuaCanvasContext *ctx = lua_canvas_create(root);
    assert(ctx);
    lua_canvas_set_context(ctx, raster.canvas, NULL, NULL, SIZE, SIZE, 1.0f);
    lua_State *L = (lua_State *)ctx->L;
    const char *script =
        "local c = sys.canvas\n"
        "assert(c.setGradient('linear', 0, 0, 64, 0, {'#FF0000', 0xFF0000FF}) == true)\n"
        "c.drawRect(0, 0, 64, 16)\n"
        "assert(c.setGradient('radial', 32, 32, 10, {'#FFF', '#000'}, {0, 1}))\n"
        "assert(c.setGradient('sweep', 32, 32, {'#F00', '#0F0'}))\n"
        "assert(c.setGradient() == true)\n"
        "assert(not pcall(c.setGradient, 'conic', 0, 0, {'#000', '#fff'}))\n"
        "assert(not pcall(c.setGradient, 'linear', 0, 0, 1, 1, {'#000'}))\n"
        "c.setFillColor('#000000')\n"
        "c.save() c.clipRoundRect(0, 16, 64, 16, 8) c.drawRect(0, 16, 64, 16) c.restore()\n"
        "local p = sys.path.create() sys.path.addRect(p, 0, 32, 8, 8)\n"
        "c.save() c.clipPath(p) c.drawRect(0, 32, 64, 16) c.restore()\n"
        "assert(not pcall(c.clipPath, 99))\n"
        "c.setFillColor('#FF0000')\n"
        "c.saveLayer(128) c.drawRect(40, 32, 20, 10) c.drawRect(40, 32, 20, 10) c.restore()\n"
        "c.saveLayer(255, 60, 0, 4, 4, 2) c.restore()\n"
        "c.setFillColor('#000000')\n"
        "local m = c.measureParagraph('one two three four', 30, 10, { lineHeight = 1 })\n"
        "assert(m.lines >= 2 and m.height == m.lines * 10 and m.width <= 30)\n"
        "local d = c.drawParagraph('one two three four', 0, 48, 64, 8, { align = 'center', maxLines = 1 })\n"
        "assert(d.lines == 1)\n"
        "assert(not pcall(c.measureParagraph, 'x', 10, 10, { align = 'middle' }))\n"
        "local r = c.measureRichText({'plain ', { text = 'big', size = 24, color = '#F00' }}, 0, { size = 8, lineHeight = 1 })\n"
        "assert(r.lines == 1 and r.height == 24)\n"
        "assert(not pcall(c.measureRichText, {{ size = 3 }}, 10))\n"
        "local svg = sys.path.create()\n"
        "assert(sys.path.addSvg(svg, 'M20 40h8v6h-8z') == true)\n"
        "assert(sys.path.addSvg(svg, 'M1 2 bogus') == false and sys.path.addSvg(99, 'M0 0') == false)\n"
        "c.setFillColor('#000000') c.drawPath(svg)\n"
        "assert(c.setFont(nil) == true and c.setFont() == true)\n";
    if (luaL_dostring(L, script) != LUA_OK)
    {
        fprintf(stderr, "Lua canvas features failure: %s\n", lua_tostring(L, -1));
        assert(false);
    }
    expect_pixel(&raster, 24, 43, 0, 0, 0, 0, "lua svg path");
    assert(is_white(&raster, 31, 43));
    expect_pixel(&raster, 0, 8, 255, 0, 0, 24, "lua linear");
    assert(is_white(&raster, 0, 16));
    expect_pixel(&raster, 32, 24, 0, 0, 0, 0, "lua round clip");
    expect_pixel(&raster, 4, 36, 0, 0, 0, 0, "lua path clip");
    assert(is_white(&raster, 20, 36));
    expect_pixel(&raster, 50, 36, 255, 127, 127, 3, "lua layer");
    assert(ink_pixels(&raster, 0, 48, SIZE, SIZE) > 5);
    lua_canvas_destroy(ctx);
    skia_surface_destroy(raster.surface);
}

static const char FEATURES_WAT[] =
    "(module\n"
    "  (import \"env\" \"canvas_set_linear_gradient\" (func $linear (param f32 f32 f32 f32 i32 i32 i32)))\n"
    "  (import \"env\" \"canvas_set_radial_gradient\" (func $radial (param f32 f32 f32 i32 i32 i32)))\n"
    "  (import \"env\" \"canvas_set_sweep_gradient\" (func $sweep (param f32 f32 i32 i32 i32)))\n"
    "  (import \"env\" \"canvas_clear_gradient\" (func $clear_gradient))\n"
    "  (import \"env\" \"canvas_set_fill_color\" (func $fill (param i32)))\n"
    "  (import \"env\" \"canvas_draw_rect\" (func $rect (param f32 f32 f32 f32)))\n"
    "  (import \"env\" \"transform_save\" (func $save))\n"
    "  (import \"env\" \"transform_restore\" (func $restore))\n"
    "  (import \"env\" \"canvas_clip_round_rect\" (func $clip_round (param f32 f32 f32 f32 f32 f32)))\n"
    "  (import \"env\" \"canvas_clip_path\" (func $clip_path (param i32)))\n"
    "  (import \"env\" \"path_create\" (func $path_create (result i32)))\n"
    "  (import \"env\" \"path_add_rect\" (func $path_rect (param i32 f32 f32 f32 f32)))\n"
    "  (import \"env\" \"canvas_save_layer\" (func $layer (param i32)))\n"
    "  (import \"env\" \"canvas_save_layer_bounds\" (func $layer_bounds (param f32 f32 f32 f32 i32 f32)))\n"
    "  (import \"env\" \"canvas_draw_paragraph\" (func $paragraph (param i32 i32 f32 f32 f32 f32 i32 f32 i32) (result f32)))\n"
    "  (import \"env\" \"canvas_measure_paragraph\" (func $measure (param i32 i32 f32 f32 f32 i32 i32) (result f32)))\n"
    "  (import \"env\" \"path_add_svg\" (func $path_svg (param i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"canvas_draw_path\" (func $draw_path (param i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (data (i32.const 96) \"M20 40h8v6h-8zM1 2 bogus\")\n"
    "  (data (i32.const 0) \"\\00\\00\\ff\\ff\\ff\\00\\00\\ff\")\n"
    "  (data (i32.const 16) \"\\00\\00\\00\\00\\00\\00\\80\\3f\")\n"
    "  (data (i32.const 32) \"one two three four\")\n"
    "  (global $lines (export \"lines\") (mut i32) (i32.const 0))\n"
    "  (func (export \"frame\") (param $time f32) (local $path i32)\n"
    "    f32.const 0 f32.const 0 f32.const 64 f32.const 0 i32.const 0 i32.const -1 i32.const 2 call $linear\n"
    "    f32.const 0 f32.const 0 f32.const 64 f32.const 16 call $rect\n"
    "    f32.const 32 f32.const 32 f32.const 10 i32.const 0 i32.const 16 i32.const 2 call $radial\n"
    "    f32.const 32 f32.const 32 i32.const 0 i32.const -1 i32.const 2 call $sweep\n"
    "    call $clear_gradient\n"
    "    i32.const 0xFF000000 call $fill\n"
    "    call $save\n"
    "    f32.const 0 f32.const 16 f32.const 64 f32.const 16 f32.const 8 f32.const 8 call $clip_round\n"
    "    f32.const 0 f32.const 16 f32.const 64 f32.const 16 call $rect\n"
    "    call $restore\n"
    "    call $path_create local.set $path\n"
    "    local.get $path f32.const 0 f32.const 32 f32.const 8 f32.const 8 call $path_rect\n"
    "    call $save local.get $path call $clip_path\n"
    "    f32.const 0 f32.const 32 f32.const 64 f32.const 16 call $rect\n"
    "    call $restore\n"
    "    i32.const 0xFFFF0000 call $fill\n"
    "    i32.const 128 call $layer\n"
    "    f32.const 40 f32.const 32 f32.const 20 f32.const 10 call $rect\n"
    "    f32.const 40 f32.const 32 f32.const 20 f32.const 10 call $rect\n"
    "    call $restore\n"
    "    f32.const 60 f32.const 0 f32.const 4 f32.const 4 i32.const 255 f32.const 2 call $layer_bounds\n"
    "    call $restore\n"
    "    i32.const 0xFF000000 call $fill\n"
    "    i32.const 32 i32.const 18 f32.const 0 f32.const 48 f32.const 64 f32.const 8 i32.const 0 f32.const 1.25 i32.const 1\n"
    "    call $paragraph drop\n"
    "    i32.const 32 i32.const 18 f32.const 30 f32.const 10 f32.const 1 i32.const 0 i32.const 64 call $measure\n"
    "    f32.const 0 f32.gt i32.eqz if unreachable end\n"
    "    i32.const 72 i32.load global.set $lines\n"
    "    call $path_create local.set $path\n"
    "    local.get $path i32.const 96 i32.const 14 call $path_svg i32.eqz if unreachable end\n"
    "    local.get $path i32.const 110 i32.const 10 call $path_svg if unreachable end\n"
    "    i32.const 99 i32.const 96 i32.const 14 call $path_svg if unreachable end\n"
    "    i32.const 0xFF000000 call $fill local.get $path call $draw_path))\n";

static void test_wasm(const char *root)
{
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/canvas-features.wat", root) > 0);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(FEATURES_WAT, 1, strlen(FEATURES_WAT), file) == strlen(FEATURES_WAT));
    assert(fclose(file) == 0);

    Raster raster;
    raster_init(&raster);
    WasmCanvasContext *ctx = wasm_canvas_create(root);
    assert(ctx);
    wasm_canvas_set_context(ctx, raster.canvas, NULL, NULL, SIZE, SIZE, 1.0f);
    if (!wasm_canvas_load_wat_file(ctx, path))
        fprintf(stderr, "Failed to load canvas features fixture: %s\n", wasm_canvas_get_error(ctx));
    assert(wasm_canvas_has_animation(ctx));
    assert(wasm_canvas_call_animation(ctx, 0.0));
    expect_pixel(&raster, 24, 43, 0, 0, 0, 0, "wasm svg path");
    assert(is_white(&raster, 31, 43));
    expect_pixel(&raster, 0, 8, 255, 0, 0, 24, "wasm linear");
    assert(is_white(&raster, 0, 16));
    expect_pixel(&raster, 32, 24, 0, 0, 0, 0, "wasm round clip");
    expect_pixel(&raster, 4, 36, 0, 0, 0, 0, "wasm path clip");
    assert(is_white(&raster, 20, 36));
    expect_pixel(&raster, 50, 36, 255, 127, 127, 3, "wasm layer");
    assert(ink_pixels(&raster, 0, 48, SIZE, SIZE) > 5);
    wasm_canvas_destroy(ctx);
    skia_surface_destroy(raster.surface);
    assert(remove(path) == 0);
}

static void test_wait_for_input(const char *root)
{
    InputState input;
    input_init(&input);

    JSRuntimeContext *runtime = js_runtime_create(root);
    JSGraphicContext *graphic = js_graphic_init(runtime);
    js_graphic_set_frame_context(graphic, NULL, &input, NULL, 100, 80, 1.0f);
    js_eval(runtime, "sys.animation.waitForInput(() => {}, 500);");
    BudoAnimationWait *wait = js_graphic_animation_wait(graphic);
    assert(wait->waiting && wait->timeout_ms == 500.0);
    assert(!budo_animation_wait_due(wait, &input, 100, 80, 1000.0));
    assert(budo_animation_wait_remaining(wait, 1200.0) == 300.0);
    assert(!budo_animation_wait_due(wait, &input, 100, 80, 1400.0));
    assert(budo_animation_wait_due(wait, &input, 100, 80, 1500.0) && !wait->waiting);

    js_eval(runtime, "sys.animation.waitForInput(() => {});");
    assert(!budo_animation_wait_due(wait, &input, 100, 80, 0.0));
    assert(budo_animation_wait_due(wait, &input, 120, 80, 1.0)); 
    js_eval(runtime, "sys.animation.waitForInput(() => {});");
    input_set_mouse_position(&input, 4, 4);
    assert(budo_animation_wait_due(wait, &input, 100, 80, 2.0)); 
    js_eval(runtime, "sys.animation.waitForInput(() => {}); sys.animation.requestFrame(() => {});");
    assert(!wait->waiting && budo_animation_wait_remaining(wait, 0.0) < 0.0);
    DeviceContext *device = js_device_init(runtime->context);
    assert(device);
    js_eval(runtime,
            "const d = sys.device;\n"
            "function threw(fn) { try { fn(); return false; } catch (e) { return e instanceof RangeError; } }\n"
            "const p = d.getPreferences();\n"
            "if (typeof p.darkMode !== 'boolean' || typeof p.reducedMotion !== 'boolean' || !(p.fontScale > 0) ||\n"
            "    typeof p.safeArea.top !== 'number' || p.keyboardInset !== 0) throw Error('preferences ' + JSON.stringify(p));\n"
            "if (d.haptic() !== false || d.haptic('success') !== false) throw Error('desktop has no haptics');\n"
            "if (!threw(() => d.haptic('thud')) || !threw(() => d.setCursor('hand'))) throw Error('names');\n"
            "if (typeof d.setCursor('pointer') !== 'boolean') throw Error('cursor');\n"
            "const clip = d.getClipboardText(); if (clip !== null && typeof clip !== 'string') throw Error('clip');\n");
    DeviceCursor cursor;
    DeviceHaptic haptic;
    assert(device_cursor_from_name("nwse-resize", &cursor) && cursor == DEVICE_CURSOR_RESIZE_NWSE);
    assert(!device_cursor_from_name("hand", &cursor));
    assert(device_haptic_from_name("error", &haptic) && haptic == DEVICE_HAPTIC_ERROR);
    assert(strcmp(device_cursor_name(DEVICE_CURSOR_TEXT), "text") == 0);
    device_binding_state_destroy(device);
    js_graphic_destroy(graphic);
    js_runtime_destroy(runtime);

    input_begin_frame(&input);
    LuaCanvasContext *lua = lua_canvas_create(root);
    lua_canvas_set_context(lua, NULL, &input, NULL, 100, 80, 1.0f);
    assert(luaL_dostring((lua_State *)lua->L, "sys.animation.waitForInput(function() end, 250)") == LUA_OK);
    wait = lua_canvas_animation_wait(lua);
    assert(wait->waiting && wait->timeout_ms == 250.0);
    assert(!budo_animation_wait_due(wait, &input, 100, 80, 0.0));
    assert(budo_animation_wait_due(wait, &input, 100, 80, 250.0));
    lua_canvas_destroy(lua);
}

static void test_accessibility(const char *root)
{
    
    AccessibilityNode nodes[3];
    int count = accessibility_parse_json(
        "[{\"id\":\"save\",\"role\":\"button\",\"label\":\"Save \\\"now\\\"\",\"x\":1,\"y\":2,\"width\":30,\"height\":10},"
        " {\"id\":\"dark\",\"role\":\"switch\",\"label\":\"Dark\",\"checked\":true,\"x\":0,\"y\":20,\"width\":30,\"height\":10},"
        " {\"id\":\"level\",\"role\":\"slider\",\"label\":\"Level\",\"min\":0,\"max\":100,\"rangeValue\":42.5,\"value\":\"42%\"}]",
        nodes, 3);
    assert(count == 3);
    assert(strcmp(nodes[0].label, "Save \"now\"") == 0 && nodes[0].checked == -1 && nodes[0].width == 30.0f);
    assert(nodes[1].checked == 1 && strcmp(nodes[1].role, "switch") == 0);
    assert(nodes[2].has_range && nodes[2].range_max == 100.0f && nodes[2].range_value == 42.5f);
    char *json = accessibility_nodes_to_json(nodes, count);
    AccessibilityNode again[3];
    assert(json && accessibility_parse_json(json, again, 3) == 3);
    assert(strcmp(again[0].label, nodes[0].label) == 0 && again[1].checked == 1 && again[2].range_value == 42.5f);
    free(json);
    assert(accessibility_parse_json("{}", again, 3) == -1);
    assert(accessibility_update(nodes, count));
#ifdef __APPLE__
    assert(accessibility_available()); 
#else
    assert(!accessibility_available());
#endif
    accessibility_post_action("save", "press", "");
    AccessibilityAction actions[4];
    assert(accessibility_take_actions(actions, 4) == 1 && strcmp(actions[0].id, "save") == 0);
    assert(accessibility_take_actions(actions, 4) == 0);

    JSRuntimeContext *runtime = js_runtime_create(root);
    JSGraphicContext *graphic = js_graphic_init(runtime);
    js_accessibility_init(runtime->context);
    accessibility_post_action("dark", "press", "");
    js_eval(runtime,
            "const a = sys.accessibility;\n"
            "if (typeof a.isAvailable() !== 'boolean' || typeof a.isActive() !== 'boolean') throw Error('availability');\n"
            "if (!a.update([{ id: 'name', role: 'textbox', label: 'Name', value: 'Ada', x: 0, y: 0, width: 50, height: 20, focused: true },\n"
            "               { id: 'go', role: 'button', label: 'Go', x: 0, y: 30, width: 50, height: 20, disabled: true }]))\n"
            "  throw Error('update');\n"
            "const actions = a.takeActions();\n"
            "if (actions.length !== 1 || actions[0].id !== 'dark' || actions[0].action !== 'press') throw Error(JSON.stringify(actions));\n"
            "let threw = false; try { a.update('nope'); } catch (e) { threw = e instanceof TypeError; }\n"
            "if (!threw) throw Error('type check');\n");
    AccessibilityNode published[4];
    assert(accessibility_copy_nodes(published, 4) == 2);
    assert(strcmp(published[0].value, "Ada") == 0 && published[0].focused && published[1].disabled);
    js_graphic_destroy(graphic);
    js_runtime_destroy(runtime);

    LuaCanvasContext *lua = lua_canvas_create(root);
    lua_accessibility_init(lua->L);
    accessibility_post_action("level", "setValue", "80");
    assert(luaL_dostring((lua_State *)lua->L,
                         "assert(sys.accessibility.update({{ id = 'a', role = 'checkbox', label = 'A', checked = false }}))\n"
                         "local actions = sys.accessibility.takeActions()\n"
                         "assert(#actions == 1 and actions[1].action == 'setValue' and actions[1].value == '80')\n") == LUA_OK);
    assert(accessibility_copy_nodes(published, 4) == 1 && published[0].checked == 0);
    lua_canvas_destroy(lua);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_wrapper();
    test_javascript(argv[1]);
    test_lua(argv[1]);
    test_wasm(argv[1]);
    test_wait_for_input(argv[1]);
    test_accessibility(argv[1]);
    puts("Canvas features tests passed");
    return 0;
}