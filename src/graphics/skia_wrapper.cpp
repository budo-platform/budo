/**
 * Skia C Wrapper - Implementation
 * Thin C++ wrappers exposed as extern "C" for C code integration
 */

#include "skia_wrapper.h"

#include "core/SkCanvas.h"
#include "core/SkSurface.h"
#include "core/SkPaint.h"
#include "core/SkPath.h"
#include "core/SkRRect.h"
#include "core/SkPathBuilder.h"
#include "utils/SkParsePath.h"
#include "core/SkColor.h"
#include "core/SkFont.h"
#include "core/SkFontMgr.h"
#include "core/SkTypeface.h"
#include "core/SkTextBlob.h"
#include "core/SkImageInfo.h"
#include "core/SkColorSpace.h"
#include "core/SkData.h"
#include "core/SkImage.h"
#include "core/SkStream.h"
#include "core/SkBlendMode.h"
#include "encode/SkPngEncoder.h"
#include "codec/SkCodec.h"

/* Platform-specific font manager includes */
#ifdef __APPLE__
#include "ports/SkFontMgr_mac_ct.h"
#elif defined(BUDO_ANDROID)
#include "ports/SkFontMgr_android.h"
#include "ports/SkFontScanner_FreeType.h"
#elif defined(BUDO_WEB)
#include "ports/SkFontMgr_empty.h"
#elif defined(__linux__)
#include "ports/SkFontMgr_fontconfig.h"
#include "ports/SkFontScanner_FreeType.h"
#endif

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <stdlib.h>

/* Cached font manager and default typeface for text rendering */
static sk_sp<SkFontMgr> g_font_mgr;
static sk_sp<SkTypeface> g_default_typeface;

/* Android GPU includes - must be outside extern "C" */
#ifdef BUDO_ANDROID
#include "gpu/ganesh/GrDirectContext.h"
#include "gpu/ganesh/gl/GrGLInterface.h"
#include "gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "gpu/ganesh/SkSurfaceGanesh.h"
#include "gpu/ganesh/gl/GrGLDirectContext.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "gpu/ganesh/GrBackendSurface.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#endif

/* WASM/WebGL GPU includes */
#ifdef BUDO_WEB
#include "gpu/ganesh/GrDirectContext.h"
#include "gpu/ganesh/gl/GrGLInterface.h"
#include "gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "gpu/ganesh/SkSurfaceGanesh.h"
#include "gpu/ganesh/gl/GrGLDirectContext.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "gpu/ganesh/GrBackendSurface.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#endif

/* Desktop GPU includes (Linux/macOS/Windows) — Skia Ganesh GL backend
 * draws straight into a window-sized GL FBO so no per-frame readback /
 * upload is needed. SDL2's GL header gives us a portable GL.h. */
#if !defined(BUDO_ANDROID) && !defined(BUDO_WEB)
#include "gpu/ganesh/GrDirectContext.h"
#include "gpu/ganesh/gl/GrGLInterface.h"
#include "gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "gpu/ganesh/SkSurfaceGanesh.h"
#include "gpu/ganesh/gl/GrGLDirectContext.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "gpu/ganesh/GrBackendSurface.h"
/* Plain glGenFramebuffers etc.: linked from the system GL library, or loaded
 * at runtime on Windows (see core/gl_desktop.h). */
#include <SDL2/SDL_video.h>
#include "core/gl_desktop.h"
#include <stdio.h>
#endif

/* ============================================
 * Internal Helper Functions
 * ============================================ */

static void *read_file_buffer(const char *filename, size_t *out_size)
{
    FILE *file;
    long size;
    void *buffer;

    if (out_size)
        *out_size = 0;
    if (!filename || !filename[0])
        return nullptr;

    file = fopen(filename, "rb");
    if (!file)
        return nullptr;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return nullptr;
    }
    size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        return nullptr;
    }
    rewind(file);

    buffer = malloc((size_t)size);
    if (!buffer)
    {
        fclose(file);
        return nullptr;
    }
    if (size > 0 && fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        free(buffer);
        fclose(file);
        return nullptr;
    }
    fclose(file);
    if (out_size)
        *out_size = (size_t)size;
    return buffer;
}

static inline SkColor toSkColor(SkiaColor c)
{
    return static_cast<SkColor>(c);
}

static inline SkRect toSkRect(float left, float top, float right, float bottom)
{
    return SkRect::MakeLTRB(left, top, right, bottom);
}

static inline SkPaint::Style toSkPaintStyle(SkiaPaintStyle style)
{
    switch (style)
    {
    case SKIA_PAINT_FILL:
        return SkPaint::kFill_Style;
    case SKIA_PAINT_STROKE:
        return SkPaint::kStroke_Style;
    case SKIA_PAINT_STROKE_AND_FILL:
        return SkPaint::kStrokeAndFill_Style;
    default:
        return SkPaint::kFill_Style;
    }
}

static inline SkPaint::Cap toSkStrokeCap(SkiaStrokeCap cap)
{
    switch (cap)
    {
    case SKIA_STROKE_CAP_BUTT:
        return SkPaint::kButt_Cap;
    case SKIA_STROKE_CAP_ROUND:
        return SkPaint::kRound_Cap;
    case SKIA_STROKE_CAP_SQUARE:
        return SkPaint::kSquare_Cap;
    default:
        return SkPaint::kButt_Cap;
    }
}

static inline SkPaint::Join toSkStrokeJoin(SkiaStrokeJoin join)
{
    switch (join)
    {
    case SKIA_STROKE_JOIN_MITER:
        return SkPaint::kMiter_Join;
    case SKIA_STROKE_JOIN_ROUND:
        return SkPaint::kRound_Join;
    case SKIA_STROKE_JOIN_BEVEL:
        return SkPaint::kBevel_Join;
    default:
        return SkPaint::kMiter_Join;
    }
}

static void ensure_font_manager()
{
    if (g_font_mgr)
    {
        return;
    }

#ifdef __APPLE__
    g_font_mgr = SkFontMgr_New_CoreText(nullptr);
#elif defined(BUDO_ANDROID)
    g_font_mgr = SkFontMgr_New_Android(nullptr, SkFontScanner_Make_FreeType());
#elif defined(BUDO_WEB)
    g_font_mgr = SkFontMgr_New_Custom_Empty();
#elif defined(__linux__)
    g_font_mgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#endif
}

static sk_sp<SkTypeface> ensure_default_typeface()
{
    if (!g_default_typeface)
    {
        ensure_font_manager();
        if (g_font_mgr)
        {
#ifdef BUDO_WEB
            /* The empty font manager has no system fonts.  Load the
             * build-time-embedded default font so text rendering works
             * out of the box for apps that don't bundle their own font. */
#if __has_include("embedded_default_font.h")
#include "embedded_default_font.h"
            auto data = SkData::MakeWithoutCopy(
                embedded_default_font_data, embedded_default_font_len);
            g_default_typeface = g_font_mgr->makeFromData(data);
#endif
#else
            g_default_typeface = g_font_mgr->legacyMakeTypeface(nullptr, SkFontStyle());
#endif
        }
    }

    return g_default_typeface;
}

/* ============================================
 * Surface Wrapper Structure
 * ============================================ */

struct SkiaSurface
{
    sk_sp<SkSurface> surface;
    int width;
    int height;
};

struct SkiaFont
{
    sk_sp<SkTypeface> typeface;
};

/* Bounded layers clip to their bounds with an extra save below the layer
 * (Skia treats layer bounds as a hint, and backdrop filters follow the clip).
 * This records, per canvas, the save counts whose next restore must also pop
 * that clip, so a single restore() ends the layer. */
static std::mutex g_layer_clip_mutex;
static std::unordered_map<SkCanvas *, std::vector<int>> g_layer_clips;

static void push_layer_clip(SkCanvas *canvas, int save_count)
{
    std::lock_guard<std::mutex> lock(g_layer_clip_mutex);
    g_layer_clips[canvas].push_back(save_count);
}

/* After a restore: true when the save count is back at a layer's clip save. */
static bool pop_layer_clip(SkCanvas *canvas)
{
    std::lock_guard<std::mutex> lock(g_layer_clip_mutex);
    auto found = g_layer_clips.find(canvas);
    if (found == g_layer_clips.end())
        return false;
    std::vector<int> &counts = found->second;
    int current = canvas->getSaveCount();
    while (!counts.empty() && counts.back() > current)
        counts.pop_back();
    bool pop = !counts.empty() && counts.back() == current;
    if (pop)
        counts.pop_back();
    if (counts.empty())
        g_layer_clips.erase(found);
    return pop;
}

static void forget_layer_clips(SkCanvas *canvas)
{
    std::lock_guard<std::mutex> lock(g_layer_clip_mutex);
    g_layer_clips.erase(canvas);
}

/* ============================================
 * Surface Management Implementation
 * ============================================ */

extern "C"
{

    SkiaSurface *skia_surface_create_raster(int width, int height)
    {
        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        sk_sp<SkSurface> surface = SkSurfaces::Raster(info);

        if (!surface)
        {
            return nullptr;
        }

        SkiaSurface *wrapper = new SkiaSurface();
        wrapper->surface = surface;
        wrapper->width = width;
        wrapper->height = height;

        return wrapper;
    }

    void skia_surface_destroy(SkiaSurface *surface)
    {
        if (surface)
        {
            if (surface->surface)
                forget_layer_clips(surface->surface->getCanvas());
            delete surface;
        }
    }

    SkiaCanvas *skia_surface_get_canvas(SkiaSurface *surface)
    {
        if (!surface || !surface->surface)
        {
            return nullptr;
        }
        return reinterpret_cast<SkiaCanvas *>(surface->surface->getCanvas());
    }

    bool skia_surface_read_pixels(SkiaSurface *surface, void *pixels)
    {
        if (!surface || !surface->surface || !pixels)
        {
            return false;
        }

        SkImageInfo info = SkImageInfo::Make(surface->width, surface->height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        return surface->surface->readPixels(info, pixels, surface->width * 4, 0, 0);
    }

    void skia_surface_get_size(SkiaSurface *surface, int *width, int *height)
    {
        if (surface)
        {
            if (width)
                *width = surface->width;
            if (height)
                *height = surface->height;
        }
    }

    /* ============================================
     * Paint Management Implementation
     * ============================================ */

    SkiaPaint *skia_paint_create(void)
    {
        SkPaint *paint = new SkPaint();
        paint->setAntiAlias(true);
        return reinterpret_cast<SkiaPaint *>(paint);
    }

    void skia_paint_destroy(SkiaPaint *paint)
    {
        if (paint)
        {
            delete reinterpret_cast<SkPaint *>(paint);
        }
    }

    void skia_paint_set_color(SkiaPaint *paint, SkiaColor color)
    {
        if (paint)
        {
            SkPaint *sk_paint = reinterpret_cast<SkPaint *>(paint);
            sk_paint->setShader(nullptr);
            sk_paint->setColor(toSkColor(color));
        }
    }

    SkiaColor skia_paint_get_color(SkiaPaint *paint)
    {
        if (paint)
        {
            return static_cast<SkiaColor>(reinterpret_cast<SkPaint *>(paint)->getColor());
        }
        return 0;
    }

    void skia_paint_set_style(SkiaPaint *paint, SkiaPaintStyle style)
    {
        if (paint)
        {
            reinterpret_cast<SkPaint *>(paint)->setStyle(toSkPaintStyle(style));
        }
    }

    void skia_paint_set_stroke_width(SkiaPaint *paint, float width)
    {
        if (paint)
        {
            reinterpret_cast<SkPaint *>(paint)->setStrokeWidth(width);
        }
    }

    float skia_paint_get_stroke_width(SkiaPaint *paint)
    {
        if (paint)
        {
            return reinterpret_cast<SkPaint *>(paint)->getStrokeWidth();
        }
        return 0.0f;
    }

    void skia_paint_set_anti_alias(SkiaPaint *paint, bool enabled)
    {
        if (paint)
        {
            reinterpret_cast<SkPaint *>(paint)->setAntiAlias(enabled);
        }
    }

    void skia_paint_set_stroke_cap(SkiaPaint *paint, SkiaStrokeCap cap)
    {
        if (paint)
        {
            reinterpret_cast<SkPaint *>(paint)->setStrokeCap(toSkStrokeCap(cap));
        }
    }

    void skia_paint_set_stroke_join(SkiaPaint *paint, SkiaStrokeJoin join)
    {
        if (paint)
        {
            reinterpret_cast<SkPaint *>(paint)->setStrokeJoin(toSkStrokeJoin(join));
        }
    }

    void skia_paint_set_alpha(SkiaPaint *paint, uint8_t alpha)
    {
        if (paint)
        {
            reinterpret_cast<SkPaint *>(paint)->setAlpha(alpha);
        }
    }

    /* ============================================
     * Path Management Implementation
     * ============================================ */

    SkiaPath *skia_path_create(void)
    {
        return reinterpret_cast<SkiaPath *>(new SkPathBuilder());
    }

    void skia_path_destroy(SkiaPath *path)
    {
        if (path)
        {
            delete reinterpret_cast<SkPathBuilder *>(path);
        }
    }

    void skia_path_reset(SkiaPath *path)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->reset();
        }
    }

    void skia_path_move_to(SkiaPath *path, float x, float y)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->moveTo(x, y);
        }
    }

    void skia_path_line_to(SkiaPath *path, float x, float y)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->lineTo(x, y);
        }
    }

    void skia_path_quad_to(SkiaPath *path, float x1, float y1, float x2, float y2)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->quadTo(x1, y1, x2, y2);
        }
    }

    void skia_path_cubic_to(SkiaPath *path, float x1, float y1, float x2, float y2, float x3, float y3)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->cubicTo(x1, y1, x2, y2, x3, y3);
        }
    }

    void skia_path_close(SkiaPath *path)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->close();
        }
    }

    void skia_path_add_rect(SkiaPath *path, float left, float top, float right, float bottom)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->addRect(toSkRect(left, top, right, bottom));
        }
    }

    void skia_path_add_oval(SkiaPath *path, float left, float top, float right, float bottom)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->addOval(toSkRect(left, top, right, bottom));
        }
    }

    void skia_path_add_circle(SkiaPath *path, float cx, float cy, float radius)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->addCircle(cx, cy, radius);
        }
    }

    bool skia_path_add_svg(SkiaPath *path, const char *data)
    {
        if (!path || !data)
            return false;
        std::optional<SkPath> parsed = SkParsePath::FromSVGString(data);
        if (!parsed)
            return false;
        reinterpret_cast<SkPathBuilder *>(path)->addPath(*parsed);
        return true;
    }

    void skia_path_add_arc(SkiaPath *path, float left, float top, float right, float bottom,
                           float start_angle, float sweep_angle)
    {
        if (path)
        {
            reinterpret_cast<SkPathBuilder *>(path)->addArc(toSkRect(left, top, right, bottom), start_angle, sweep_angle);
        }
    }

    /* ============================================
     * Canvas Drawing Implementation
     * ============================================ */

    void skia_canvas_clear(SkiaCanvas *canvas, SkiaColor color)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->clear(toSkColor(color));
        }
    }

    void skia_canvas_draw_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawRect(
                toSkRect(left, top, right, bottom),
                *reinterpret_cast<SkPaint *>(paint));
        }
    }

    void skia_canvas_draw_round_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom,
                                     float rx, float ry, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawRoundRect(
                toSkRect(left, top, right, bottom),
                rx, ry,
                *reinterpret_cast<SkPaint *>(paint));
        }
    }

    void skia_canvas_draw_circle(SkiaCanvas *canvas, float cx, float cy, float radius, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawCircle(cx, cy, radius, *reinterpret_cast<SkPaint *>(paint));
        }
    }

    void skia_canvas_draw_oval(SkiaCanvas *canvas, float left, float top, float right, float bottom, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawOval(
                toSkRect(left, top, right, bottom),
                *reinterpret_cast<SkPaint *>(paint));
        }
    }

    void skia_canvas_draw_line(SkiaCanvas *canvas, float x1, float y1, float x2, float y2, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawLine(x1, y1, x2, y2, *reinterpret_cast<SkPaint *>(paint));
        }
    }

    void skia_canvas_draw_path(SkiaCanvas *canvas, SkiaPath *path, SkiaPaint *paint)
    {
        if (canvas && path && paint)
        {
            SkPath skPath = reinterpret_cast<SkPathBuilder *>(path)->snapshot();
            reinterpret_cast<SkCanvas *>(canvas)->drawPath(
                skPath,
                *reinterpret_cast<SkPaint *>(paint));
        }
    }

    void skia_canvas_draw_point(SkiaCanvas *canvas, float x, float y, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawPoint(x, y, *reinterpret_cast<SkPaint *>(paint));
        }
    }

    SkiaFont *skia_font_load_file(const char *filename)
    {
        size_t size = 0;
        void *data = read_file_buffer(filename, &size);
        SkiaFont *font;

        if (!data)
            return nullptr;

        font = skia_font_load_buffer(data, size);
        free(data);
        return font;
    }

    SkiaFont *skia_font_load_buffer(const void *data, size_t size)
    {
        sk_sp<SkTypeface> typeface;

        if (!data || size == 0)
            return nullptr;

        ensure_font_manager();
        if (!g_font_mgr)
        {
            return nullptr;
        }

        typeface = g_font_mgr->makeFromData(SkData::MakeWithCopy(data, size), 0);
        if (!typeface)
        {
            return nullptr;
        }

#ifdef BUDO_WEB
        /* On web/WASM the empty font manager has no system fonts, so
         * g_default_typeface is null.  Use the first successfully loaded
         * font file as the fallback default so that drawText() without
         * an explicit font still renders something. */
        if (!g_default_typeface)
        {
            g_default_typeface = typeface;
        }
#endif

        SkiaFont *font = new SkiaFont();
        font->typeface = typeface;
        return font;
    }

    void skia_font_destroy(SkiaFont *font)
    {
        delete font;
    }

    float skia_canvas_draw_text(SkiaCanvas *canvas, const char *text, float x, float y, float font_size, SkiaPaint *paint)
    {
        return skia_canvas_draw_text_with_font(canvas, text, x, y, font_size, paint, nullptr);
    }

    float skia_canvas_draw_text_with_font(SkiaCanvas *canvas, const char *text, float x, float y, float font_size,
                                          SkiaPaint *paint, SkiaFont *font)
    {
        if (canvas && text && paint)
        {
            sk_sp<SkTypeface> typeface = font && font->typeface ? font->typeface : ensure_default_typeface();
            SkFont sk_font(typeface, font_size);
            sk_font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
            sk_font.setSubpixel(true);
            sk_font.setHinting(SkFontHinting::kSlight);

            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(text, sk_font);
            if (blob)
            {
                reinterpret_cast<SkCanvas *>(canvas)->drawTextBlob(
                    blob,
                    x, y,
                    *reinterpret_cast<SkPaint *>(paint));
                return sk_font.measureText(text, strlen(text), SkTextEncoding::kUTF8, nullptr);
            }
        }
        return 0.0f;
    }

    float skia_measure_text_width(const char *text, float font_size, SkiaFont *font)
    {
        if (!text)
            return 0.0f;
        sk_sp<SkTypeface> typeface = font && font->typeface ? font->typeface : ensure_default_typeface();
        SkFont sk_font(typeface, font_size);
        sk_font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        sk_font.setSubpixel(true);
        sk_font.setHinting(SkFontHinting::kSlight);
        return sk_font.measureText(text, strlen(text), SkTextEncoding::kUTF8, nullptr);
    }

    void skia_measure_text_rect(const char *text, float font_size, SkiaFont *font, float *out_width, float *out_height)
    {
        if (!text || (!out_width && !out_height))
            return;
        sk_sp<SkTypeface> typeface = font && font->typeface ? font->typeface : ensure_default_typeface();
        SkFont sk_font(typeface, font_size);
        sk_font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        sk_font.setSubpixel(true);
        sk_font.setHinting(SkFontHinting::kSlight);
        SkRect bounds;
        float width = sk_font.measureText(text, strlen(text), SkTextEncoding::kUTF8, &bounds);
        if (out_width)
            *out_width = width;
        if (out_height)
            *out_height = bounds.height();
    }

    void skia_canvas_draw_arc(SkiaCanvas *canvas, float left, float top, float right, float bottom,
                              float start_angle, float sweep_angle, bool use_center, SkiaPaint *paint)
    {
        if (canvas && paint)
        {
            reinterpret_cast<SkCanvas *>(canvas)->drawArc(
                toSkRect(left, top, right, bottom),
                start_angle, sweep_angle,
                use_center,
                *reinterpret_cast<SkPaint *>(paint));
        }
    }

    /* ============================================
     * Canvas Transformation Implementation
     * ============================================ */

    void skia_canvas_save(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->save();
        }
    }

    void skia_canvas_restore(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            SkCanvas *sk_canvas = reinterpret_cast<SkCanvas *>(canvas);
            sk_canvas->restore();
            if (pop_layer_clip(sk_canvas))
                sk_canvas->restore();
        }
    }

    int skia_canvas_get_save_count(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            return reinterpret_cast<SkCanvas *>(canvas)->getSaveCount();
        }
        return 0;
    }

    void skia_canvas_restore_to_count(SkiaCanvas *canvas, int count)
    {
        if (canvas)
        {
            SkCanvas *sk_canvas = reinterpret_cast<SkCanvas *>(canvas);
            sk_canvas->restoreToCount(count);
            pop_layer_clip(sk_canvas);
        }
    }

    void skia_canvas_translate(SkiaCanvas *canvas, float dx, float dy)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->translate(dx, dy);
        }
    }

    void skia_canvas_scale(SkiaCanvas *canvas, float sx, float sy)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->scale(sx, sy);
        }
    }

    void skia_canvas_rotate(SkiaCanvas *canvas, float degrees)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->rotate(degrees);
        }
    }

    void skia_canvas_rotate_around(SkiaCanvas *canvas, float degrees, float px, float py)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->rotate(degrees, px, py);
        }
    }

    void skia_canvas_skew(SkiaCanvas *canvas, float sx, float sy)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->skew(sx, sy);
        }
    }

    void skia_canvas_reset_transform(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->resetMatrix();
        }
    }

    /* ============================================
     * Canvas Clipping Implementation
     * ============================================ */

    void skia_canvas_clip_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->clipRect(toSkRect(left, top, right, bottom));
        }
    }

    void skia_canvas_clip_path(SkiaCanvas *canvas, SkiaPath *path)
    {
        if (canvas && path)
        {
            SkPath sk_path = reinterpret_cast<SkPathBuilder *>(path)->snapshot();
            reinterpret_cast<SkCanvas *>(canvas)->clipPath(sk_path, true);
        }
    }

    void skia_canvas_clip_round_rect(SkiaCanvas *canvas, float left, float top, float right, float bottom,
                                     float rx, float ry)
    {
        if (canvas)
        {
            reinterpret_cast<SkCanvas *>(canvas)->clipRRect(
                SkRRect::MakeRectXY(toSkRect(left, top, right, bottom), rx, ry), true);
        }
    }

    /* ============================================
     * GPU/OpenGL Canvas Implementation (Android)
     * ============================================ */

#ifdef BUDO_ANDROID

#define LOG_TAG "SkiaWrapper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

    struct SkiaGLCanvas
    {
        sk_sp<GrDirectContext> context;
        sk_sp<SkSurface> surface;
        SkCanvas *canvas;
        GLuint texture;
        GLuint fbo;
        GLuint depth_stencil_rbo;
        int width;
        int height;
        SkiaGLCanvas *next;
    };

    static SkiaGLCanvas *g_android_gl_canvas_list = nullptr;

    static SkiaGLCanvas *find_android_gl_canvas(SkCanvas *sk_canvas)
    {
        for (SkiaGLCanvas *w = g_android_gl_canvas_list; w; w = w->next)
        {
            if (w->canvas == sk_canvas)
                return w;
        }
        return nullptr;
    }

    static void register_android_gl_canvas(SkiaGLCanvas *w)
    {
        w->next = g_android_gl_canvas_list;
        g_android_gl_canvas_list = w;
    }

    static void unregister_android_gl_canvas(SkiaGLCanvas *target)
    {
        SkiaGLCanvas **link = &g_android_gl_canvas_list;
        while (*link)
        {
            if (*link == target)
            {
                *link = target->next;
                return;
            }
            link = &(*link)->next;
        }
    }

    SkiaCanvas *skia_canvas_create_gl(int width, int height)
    {
        LOGI("Creating GL canvas %dx%d", width, height);

        // Create Skia GL interface
        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
        if (!interface)
        {
            LOGE("Failed to create GrGLInterface");
            return nullptr;
        }

        // Create Skia GPU context
        sk_sp<GrDirectContext> context = GrDirectContexts::MakeGL(interface);
        if (!context)
        {
            LOGE("Failed to create GrDirectContext");
            return nullptr;
        }

        // Get the current framebuffer
        GLint framebuffer;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

        // Get stencil bits
        GLint stencilBits;
        glGetIntegerv(GL_STENCIL_BITS, &stencilBits);

        // Create render target info
        GrGLFramebufferInfo fbInfo;
        fbInfo.fFBOID = framebuffer;
        fbInfo.fFormat = GL_RGBA8;

        // Create backend render target
        auto backendRT = GrBackendRenderTargets::MakeGL(
            width, height,
            0, // sample count
            stencilBits,
            fbInfo);

        // Create surface from render target
        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context.get(),
            backendRT,
            kBottomLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType,
            nullptr, // colorspace
            &props);

        if (!surface)
        {
            LOGE("Failed to create SkSurface from GL render target");
            return nullptr;
        }

        SkiaGLCanvas *wrapper = new SkiaGLCanvas();
        wrapper->context = context;
        wrapper->surface = surface;
        wrapper->canvas = surface->getCanvas();
        wrapper->texture = 0;
        wrapper->fbo = 0;
        wrapper->depth_stencil_rbo = 0;
        wrapper->width = width;
        wrapper->height = height;
        register_android_gl_canvas(wrapper);

        LOGI("GL canvas created successfully");
        return reinterpret_cast<SkiaCanvas *>(wrapper->canvas);
    }

    SkiaCanvas *skia_canvas_create_gl_offscreen(int width, int height,
                                                unsigned int *out_gl_texture_id,
                                                unsigned int *out_gl_fbo_id)
    {
        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
        if (!interface)
        {
            LOGE("Failed to create GrGLInterface (offscreen)");
            return nullptr;
        }

        sk_sp<GrDirectContext> context = GrDirectContexts::MakeGL(interface);
        if (!context)
        {
            LOGE("Failed to create GrDirectContext (offscreen)");
            return nullptr;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        if (tex == 0)
        {
            LOGE("glGenTextures failed (offscreen)");
            return nullptr;
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        if (fbo == 0)
        {
            LOGE("glGenFramebuffers failed (offscreen)");
            glDeleteTextures(1, &tex);
            return nullptr;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);

        GLuint depth_stencil = 0;
        glGenRenderbuffers(1, &depth_stencil);
        if (depth_stencil != 0)
        {
            glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, depth_stencil);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            LOGE("FBO incomplete (status=0x%x)", status);
            if (depth_stencil != 0)
                glDeleteRenderbuffers(1, &depth_stencil);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return nullptr;
        }

        GrGLFramebufferInfo fbInfo;
        fbInfo.fFBOID = fbo;
        fbInfo.fFormat = GL_RGBA8;

        auto backendRT = GrBackendRenderTargets::MakeGL(width, height, 0,
                                                        depth_stencil != 0 ? 8 : 0,
                                                        fbInfo);

        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context.get(), backendRT, kTopLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType, nullptr, &props);
        if (!surface)
        {
            LOGE("Failed to wrap offscreen FBO as SkSurface");
            if (depth_stencil != 0)
                glDeleteRenderbuffers(1, &depth_stencil);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return nullptr;
        }

        SkiaGLCanvas *wrapper = new SkiaGLCanvas();
        wrapper->context = context;
        wrapper->surface = surface;
        wrapper->canvas = surface->getCanvas();
        wrapper->texture = tex;
        wrapper->fbo = fbo;
        wrapper->depth_stencil_rbo = depth_stencil;
        wrapper->width = width;
        wrapper->height = height;
        register_android_gl_canvas(wrapper);

        if (out_gl_texture_id)
            *out_gl_texture_id = (unsigned int)tex;
        if (out_gl_fbo_id)
            *out_gl_fbo_id = (unsigned int)fbo;

        return reinterpret_cast<SkiaCanvas *>(wrapper->canvas);
    }

    void skia_canvas_destroy(SkiaCanvas *canvas)
    {
        if (!canvas)
            return;
        forget_layer_clips(reinterpret_cast<SkCanvas *>(canvas));
        SkiaGLCanvas *w = find_android_gl_canvas(reinterpret_cast<SkCanvas *>(canvas));
        if (!w)
            return;
        unregister_android_gl_canvas(w);
        w->surface.reset();
        if (w->context)
        {
            w->context->flushAndSubmit();
            w->context.reset();
        }
        if (w->depth_stencil_rbo != 0)
            glDeleteRenderbuffers(1, &w->depth_stencil_rbo);
        if (w->fbo != 0)
            glDeleteFramebuffers(1, &w->fbo);
        if (w->texture != 0)
            glDeleteTextures(1, &w->texture);
        delete w;
    }

    void skia_canvas_flush(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            // Get the surface's context and flush it
            // Note: We need access to the original context to flush properly
            // For now, we use Skia's canvas flush which submits pending commands
            SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);

            // Get the recording context (GPU context) from the canvas
            auto recordingContext = skCanvas->recordingContext();
            if (recordingContext)
            {
                GrDirectContext *directContext = recordingContext->asDirectContext();
                if (directContext)
                {
                    /* Raw GL calls (sys.gl draws, bindScreen, render targets) may have
                     * rebound framebuffers and textures since Skia last ran: re-read the
                     * GL state so this flush draws into Skia's own surface. */
                    directContext->resetContext();
                    // Submit all pending work to the GPU
                    directContext->flushAndSubmit();
                }
            }
        }
    }

    void skia_canvas_reset_gl_context(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);
            auto recordingContext = skCanvas->recordingContext();
            if (recordingContext)
            {
                GrDirectContext *directContext = recordingContext->asDirectContext();
                if (directContext)
                {
                    directContext->resetContext();
                }
            }
        }
    }

#endif // BUDO_ANDROID

    /* ============================================
     * GPU/OpenGL Canvas Implementation (WASM/WebGL)
     * ============================================ */

#ifdef BUDO_WEB

    struct SkiaGLCanvas
    {
        sk_sp<GrDirectContext> context;
        sk_sp<SkSurface> surface;
        SkCanvas *canvas;
        GLuint texture;
        GLuint fbo;
        GLuint depth_stencil_rbo;
        int width;
        int height;
        SkiaGLCanvas *next;
    };

    static SkiaGLCanvas *g_web_gl_canvas_list = nullptr;

    static SkiaGLCanvas *find_web_gl_canvas(SkCanvas *sk_canvas)
    {
        for (SkiaGLCanvas *w = g_web_gl_canvas_list; w; w = w->next)
        {
            if (w->canvas == sk_canvas)
                return w;
        }
        return nullptr;
    }

    static void register_web_gl_canvas(SkiaGLCanvas *w)
    {
        w->next = g_web_gl_canvas_list;
        g_web_gl_canvas_list = w;
    }

    static void unregister_web_gl_canvas(SkiaGLCanvas *target)
    {
        SkiaGLCanvas **link = &g_web_gl_canvas_list;
        while (*link)
        {
            if (*link == target)
            {
                *link = target->next;
                return;
            }
            link = &(*link)->next;
        }
    }

    SkiaCanvas *skia_canvas_create_gl(int width, int height)
    {
        printf("[skia-wasm] Creating GL canvas %dx%d\n", width, height);

        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
        if (!interface)
        {
            fprintf(stderr, "[skia-wasm] Failed to create GrGLInterface\n");
            return nullptr;
        }

        sk_sp<GrDirectContext> context = GrDirectContexts::MakeGL(interface);
        if (!context)
        {
            fprintf(stderr, "[skia-wasm] Failed to create GrDirectContext\n");
            return nullptr;
        }

        GLint framebuffer;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

        GLint stencilBits;
        glGetIntegerv(GL_STENCIL_BITS, &stencilBits);

        GrGLFramebufferInfo fbInfo;
        fbInfo.fFBOID = framebuffer;
        fbInfo.fFormat = GL_RGBA8;

        auto backendRT = GrBackendRenderTargets::MakeGL(
            width, height,
            0,
            stencilBits,
            fbInfo);

        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context.get(),
            backendRT,
            kBottomLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType,
            nullptr,
            &props);

        if (!surface)
        {
            fprintf(stderr, "[skia-wasm] Failed to create SkSurface from GL render target\n");
            return nullptr;
        }

        SkiaGLCanvas *wrapper = new SkiaGLCanvas();
        wrapper->context = context;
        wrapper->surface = surface;
        wrapper->canvas = surface->getCanvas();
        wrapper->texture = 0;
        wrapper->fbo = 0;
        wrapper->depth_stencil_rbo = 0;
        wrapper->width = width;
        wrapper->height = height;
        register_web_gl_canvas(wrapper);

        printf("[skia-wasm] GL canvas created successfully\n");
        return reinterpret_cast<SkiaCanvas *>(wrapper->canvas);
    }

    SkiaCanvas *skia_canvas_create_gl_offscreen(int width, int height,
                                                unsigned int *out_gl_texture_id,
                                                unsigned int *out_gl_fbo_id)
    {
        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
        if (!interface)
        {
            fprintf(stderr, "[skia-wasm] Failed to create GrGLInterface (offscreen)\n");
            return nullptr;
        }

        sk_sp<GrDirectContext> context = GrDirectContexts::MakeGL(interface);
        if (!context)
        {
            fprintf(stderr, "[skia-wasm] Failed to create GrDirectContext (offscreen)\n");
            return nullptr;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        if (tex == 0)
        {
            fprintf(stderr, "[skia-wasm] glGenTextures failed (offscreen)\n");
            return nullptr;
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        if (fbo == 0)
        {
            fprintf(stderr, "[skia-wasm] glGenFramebuffers failed (offscreen)\n");
            glDeleteTextures(1, &tex);
            return nullptr;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);

        GLuint depth_stencil = 0;
        glGenRenderbuffers(1, &depth_stencil);
        if (depth_stencil != 0)
        {
            glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, depth_stencil);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            fprintf(stderr, "[skia-wasm] FBO incomplete (status=0x%x)\n", status);
            if (depth_stencil != 0)
                glDeleteRenderbuffers(1, &depth_stencil);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return nullptr;
        }

        GrGLFramebufferInfo fbInfo;
        fbInfo.fFBOID = fbo;
        fbInfo.fFormat = GL_RGBA8;

        auto backendRT = GrBackendRenderTargets::MakeGL(width, height, 0,
                                                        depth_stencil != 0 ? 8 : 0,
                                                        fbInfo);

        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context.get(), backendRT, kTopLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType, nullptr, &props);
        if (!surface)
        {
            fprintf(stderr, "[skia-wasm] Failed to wrap offscreen FBO as SkSurface\n");
            if (depth_stencil != 0)
                glDeleteRenderbuffers(1, &depth_stencil);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return nullptr;
        }

        SkiaGLCanvas *wrapper = new SkiaGLCanvas();
        wrapper->context = context;
        wrapper->surface = surface;
        wrapper->canvas = surface->getCanvas();
        wrapper->texture = tex;
        wrapper->fbo = fbo;
        wrapper->depth_stencil_rbo = depth_stencil;
        wrapper->width = width;
        wrapper->height = height;
        register_web_gl_canvas(wrapper);

        if (out_gl_texture_id)
            *out_gl_texture_id = (unsigned int)tex;
        if (out_gl_fbo_id)
            *out_gl_fbo_id = (unsigned int)fbo;

        return reinterpret_cast<SkiaCanvas *>(wrapper->canvas);
    }

    void skia_canvas_destroy(SkiaCanvas *canvas)
    {
        if (!canvas)
            return;
        forget_layer_clips(reinterpret_cast<SkCanvas *>(canvas));
        SkiaGLCanvas *w = find_web_gl_canvas(reinterpret_cast<SkCanvas *>(canvas));
        if (!w)
            return;
        unregister_web_gl_canvas(w);
        w->surface.reset();
        if (w->context)
        {
            w->context->flushAndSubmit();
            w->context.reset();
        }
        if (w->depth_stencil_rbo != 0)
            glDeleteRenderbuffers(1, &w->depth_stencil_rbo);
        if (w->fbo != 0)
            glDeleteFramebuffers(1, &w->fbo);
        if (w->texture != 0)
            glDeleteTextures(1, &w->texture);
        delete w;
    }

    void skia_canvas_flush(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);
            auto recordingContext = skCanvas->recordingContext();
            if (recordingContext)
            {
                GrDirectContext *directContext = recordingContext->asDirectContext();
                if (directContext)
                {
                    /* Raw GL calls (sys.gl draws, bindScreen, render targets) may have
                     * rebound framebuffers and textures since Skia last ran: re-read the
                     * GL state so this flush draws into Skia's own surface. */
                    directContext->resetContext();
                    directContext->flushAndSubmit();
                }
            }
        }
    }

    void skia_canvas_reset_gl_context(SkiaCanvas *canvas)
    {
        if (canvas)
        {
            SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);
            auto recordingContext = skCanvas->recordingContext();
            if (recordingContext)
            {
                GrDirectContext *directContext = recordingContext->asDirectContext();
                if (directContext)
                {
                    directContext->resetContext();
                }
            }
        }
    }

#endif // BUDO_WEB

    /* ============================================
     * GPU/OpenGL Canvas Implementation (Desktop: Linux/macOS/Windows)
     * ============================================ */

#if !defined(BUDO_ANDROID) && !defined(BUDO_WEB)

    struct SkiaDesktopGLCanvas
    {
        sk_sp<GrDirectContext> context;
        sk_sp<SkSurface> surface;
        SkCanvas *canvas;
        GLuint texture; /* 0 if wrapping default framebuffer */
        GLuint fbo;     /* 0 if wrapping default framebuffer */
        int width;
        int height;
        SkiaDesktopGLCanvas *next;
    };

    /* Singly-linked list of every desktop GL canvas we created so destroy /
     * flush / reset can recover the wrapper from a bare SkCanvas pointer. */
    static SkiaDesktopGLCanvas *g_desktop_gl_canvas_list = nullptr;

    static SkiaDesktopGLCanvas *find_desktop_gl_canvas(SkCanvas *sk_canvas)
    {
        for (SkiaDesktopGLCanvas *w = g_desktop_gl_canvas_list; w; w = w->next)
        {
            if (w->canvas == sk_canvas)
                return w;
        }
        return nullptr;
    }

    static void register_desktop_gl_canvas(SkiaDesktopGLCanvas *w)
    {
        w->next = g_desktop_gl_canvas_list;
        g_desktop_gl_canvas_list = w;
    }

    static void unregister_desktop_gl_canvas(SkiaDesktopGLCanvas *target)
    {
        SkiaDesktopGLCanvas **link = &g_desktop_gl_canvas_list;
        while (*link)
        {
            if (*link == target)
            {
                *link = target->next;
                return;
            }
            link = &(*link)->next;
        }
    }

    static GrGLFuncPtr skia_desktop_get_gl_proc(void *, const char name[])
    {
        if (strncmp(name, "egl", 3) == 0)
            return nullptr;
        return (GrGLFuncPtr)SDL_GL_GetProcAddress(name);
    }

    /* C++ linkage: MSVC rejects C++ return types inside extern "C". */
    extern "C++"
    {
        static sk_sp<const GrGLInterface> skia_desktop_make_gl_interface()
        {
            sk_sp<const GrGLInterface> interface = GrGLMakeAssembledInterface(nullptr, skia_desktop_get_gl_proc);
            if (interface)
                return interface;
            return GrGLMakeNativeInterface();
        }
    }

    SkiaCanvas *skia_canvas_create_gl(int width, int height)
    {
        sk_sp<const GrGLInterface> interface = skia_desktop_make_gl_interface();
        if (!interface)
        {
            fprintf(stderr, "[skia-desktop] Failed to create GrGLInterface\n");
            return nullptr;
        }

        sk_sp<GrDirectContext> context = GrDirectContexts::MakeGL(interface);
        if (!context)
        {
            fprintf(stderr, "[skia-desktop] Failed to create GrDirectContext\n");
            return nullptr;
        }

        GLint framebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

        GLint stencilBits = 0;
        glGetIntegerv(GL_STENCIL_BITS, &stencilBits);

        GrGLFramebufferInfo fbInfo;
        fbInfo.fFBOID = (GrGLuint)framebuffer;
        fbInfo.fFormat = GL_RGBA8;

        auto backendRT = GrBackendRenderTargets::MakeGL(width, height, 0, stencilBits, fbInfo);

        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context.get(), backendRT, kBottomLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType, nullptr, &props);
        if (!surface)
        {
            fprintf(stderr, "[skia-desktop] Failed to wrap default framebuffer\n");
            return nullptr;
        }

        SkiaDesktopGLCanvas *w = new SkiaDesktopGLCanvas();
        w->context = context;
        w->surface = surface;
        w->canvas = surface->getCanvas();
        w->texture = 0;
        w->fbo = 0;
        w->width = width;
        w->height = height;
        register_desktop_gl_canvas(w);
        return reinterpret_cast<SkiaCanvas *>(w->canvas);
    }

    SkiaCanvas *skia_canvas_create_gl_offscreen(int width, int height,
                                                unsigned int *out_gl_texture_id,
                                                unsigned int *out_gl_fbo_id)
    {
        sk_sp<const GrGLInterface> interface = skia_desktop_make_gl_interface();
        if (!interface)
        {
            fprintf(stderr, "[skia-desktop] Failed to create GrGLInterface (offscreen)\n");
            return nullptr;
        }

        sk_sp<GrDirectContext> context = GrDirectContexts::MakeGL(interface);
        if (!context)
        {
            fprintf(stderr, "[skia-desktop] Failed to create GrDirectContext (offscreen)\n");
            return nullptr;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        if (tex == 0)
        {
            fprintf(stderr, "[skia-desktop] glGenTextures failed (offscreen)\n");
            return nullptr;
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        if (fbo == 0)
        {
            fprintf(stderr, "[skia-desktop] glGenFramebuffers failed (offscreen)\n");
            glDeleteTextures(1, &tex);
            return nullptr;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            fprintf(stderr, "[skia-desktop] FBO incomplete (status=0x%x)\n", status);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            return nullptr;
        }

        GrGLFramebufferInfo fbInfo;
        fbInfo.fFBOID = (GrGLuint)fbo;
        fbInfo.fFormat = GL_RGBA8;

        auto backendRT = GrBackendRenderTargets::MakeGL(width, height, 0, 0, fbInfo);

        SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
        /* kTopLeft origin: Skia's visual (0,0) lands at GL texture row 0.
         * Sampling tex.y=0 then returns the visual top of the canvas, which
         * matches the existing fullscreen-blit UV mapping that the previous
         * raster path also relied on. */
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context.get(), backendRT, kTopLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType, nullptr, &props);
        if (!surface)
        {
            fprintf(stderr, "[skia-desktop] Failed to wrap offscreen FBO as SkSurface\n");
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            return nullptr;
        }

        SkiaDesktopGLCanvas *w = new SkiaDesktopGLCanvas();
        w->context = context;
        w->surface = surface;
        w->canvas = surface->getCanvas();
        w->texture = tex;
        w->fbo = fbo;
        w->width = width;
        w->height = height;
        register_desktop_gl_canvas(w);

        if (out_gl_texture_id)
            *out_gl_texture_id = (unsigned int)tex;
        if (out_gl_fbo_id)
            *out_gl_fbo_id = (unsigned int)fbo;

        return reinterpret_cast<SkiaCanvas *>(w->canvas);
    }

    void skia_canvas_destroy(SkiaCanvas *canvas)
    {
        if (!canvas)
            return;
        forget_layer_clips(reinterpret_cast<SkCanvas *>(canvas));
        SkiaDesktopGLCanvas *w = find_desktop_gl_canvas(reinterpret_cast<SkCanvas *>(canvas));
        if (!w)
            return;
        unregister_desktop_gl_canvas(w);
        /* Drop the surface FIRST so Skia releases its hold on the FBO before
         * we delete the GL objects (otherwise Skia may try to free them on
         * context teardown). */
        w->surface.reset();
        if (w->context)
        {
            w->context->flushAndSubmit();
            w->context.reset();
        }
        if (w->fbo != 0)
            glDeleteFramebuffers(1, &w->fbo);
        if (w->texture != 0)
            glDeleteTextures(1, &w->texture);
        delete w;
    }

    void skia_canvas_flush(SkiaCanvas *canvas)
    {
        if (!canvas)
            return;
        SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);
        auto recordingContext = skCanvas->recordingContext();
        if (recordingContext)
        {
            GrDirectContext *directContext = recordingContext->asDirectContext();
            if (directContext)
            {
                /* Raw GL calls (sys.gl draws, bindScreen, render targets) may have
                 * rebound framebuffers and textures since Skia last ran: re-read the
                 * GL state so this flush draws into Skia's own surface. */
                directContext->resetContext();
                directContext->flushAndSubmit();
            }
        }
    }

    void skia_canvas_reset_gl_context(SkiaCanvas *canvas)
    {
        if (!canvas)
            return;
        SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);
        auto recordingContext = skCanvas->recordingContext();
        if (recordingContext)
        {
            GrDirectContext *directContext = recordingContext->asDirectContext();
            if (directContext)
                directContext->resetContext();
        }
    }

#endif // !BUDO_ANDROID && !BUDO_WEB

    /* ============================================
     * Cross-platform GPU canvas size / read-back
     * Works for any SkiaCanvas* whose underlying SkCanvas has a backing
     * surface (raster or GPU). Used to expose sys.canvas.readPixels() to JS / Lua / WASM.
     * ============================================ */

    bool skia_canvas_get_size(SkiaCanvas *canvas, int *out_width, int *out_height)
    {
        if (!canvas)
            return false;
        SkCanvas *sk = reinterpret_cast<SkCanvas *>(canvas);
        SkImageInfo info = sk->imageInfo();
        if (info.width() <= 0 || info.height() <= 0)
            return false;
        if (out_width)
            *out_width = info.width();
        if (out_height)
            *out_height = info.height();
        return true;
    }

    bool skia_canvas_read_pixels(SkiaCanvas *canvas, void *out_pixels)
    {
        if (!canvas || !out_pixels)
            return false;
        SkCanvas *sk = reinterpret_cast<SkCanvas *>(canvas);
        int w = sk->imageInfo().width();
        int h = sk->imageInfo().height();
        if (w <= 0 || h <= 0)
            return false;

        /* Force any pending Skia work to the GPU before reading back. The
         * flush also re-reads GL state that raw GL drawing earlier in the
         * frame changed; without that Skia would read whatever framebuffer is
         * bound now (the screen, bottom-up) as if it were its own surface. */
        skia_canvas_flush(canvas);

        SkImageInfo dst = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        return sk->readPixels(dst, out_pixels, (size_t)w * 4, 0, 0);
    }

    /* ============================================
     * SVG Support Implementation
     * ============================================ */

} // extern "C"

#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "core/SkStream.h"

struct SkiaSVGData
{
    sk_sp<SkSVGDOM> dom;
    float width;
    float height;
};

extern "C"
{

    SkiaSVG *skia_svg_load_file(const char *filename)
    {
        size_t size = 0;
        void *data = read_file_buffer(filename, &size);
        SkiaSVG *svg;

        if (!data)
            return nullptr;

        svg = skia_svg_load_buffer(data, size);
        free(data);
        return svg;
    }

    SkiaSVG *skia_svg_load_buffer(const void *data, size_t size)
    {
        if (!data || size == 0)
            return nullptr;

        SkMemoryStream stream(data, size, false);

        auto dom = SkSVGDOM::MakeFromStream(stream);
        if (!dom)
        {
            return nullptr;
        }

        SkSize intrinsic = dom->containerSize();
        if (intrinsic.isEmpty())
        {
            /* Fall back: set a default container size so render is not empty */
            intrinsic = SkSize::Make(100, 100);
            dom->setContainerSize(intrinsic);
        }

        SkiaSVGData *svg = new SkiaSVGData();
        svg->dom = dom;
        svg->width = intrinsic.width();
        svg->height = intrinsic.height();

        return reinterpret_cast<SkiaSVG *>(svg);
    }

    void skia_svg_destroy(SkiaSVG *svg)
    {
        if (svg)
        {
            delete reinterpret_cast<SkiaSVGData *>(svg);
        }
    }

    float skia_svg_get_width(SkiaSVG *svg)
    {
        if (svg)
        {
            return reinterpret_cast<SkiaSVGData *>(svg)->width;
        }
        return 0.0f;
    }

    float skia_svg_get_height(SkiaSVG *svg)
    {
        if (svg)
        {
            return reinterpret_cast<SkiaSVGData *>(svg)->height;
        }
        return 0.0f;
    }

    void skia_svg_render(SkiaSVG *svg, SkiaCanvas *canvas, float x, float y, float width, float height)
    {
        if (!svg || !canvas)
        {
            return;
        }

        SkiaSVGData *data = reinterpret_cast<SkiaSVGData *>(svg);
        SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);

        skCanvas->save();
        skCanvas->translate(x, y);

        /* Scale SVG to fit the requested size */
        if (data->width > 0 && data->height > 0 && width > 0 && height > 0)
        {
            float sx = width / data->width;
            float sy = height / data->height;
            skCanvas->scale(sx, sy);
        }

        data->dom->setContainerSize(SkSize::Make(data->width, data->height));
        data->dom->render(skCanvas);

        skCanvas->restore();
    }

    /* ============================================
     * Image I/O Implementation
     * ============================================ */

    bool skia_image_draw_file(SkiaCanvas *canvas, const char *filename,
                              float x, float y, float width, float height)
    {
        if (!canvas || !filename)
        {
            return false;
        }

        sk_sp<SkData> encoded = SkData::MakeFromFileName(filename);
        if (!encoded)
        {
            return false;
        }

        sk_sp<SkImage> image;
        std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(encoded);
        if (codec)
        {
            auto result = codec->getImage();
            image = std::get<0>(result);
        }
        if (!image)
        {
            return false;
        }

        SkCanvas *skCanvas = reinterpret_cast<SkCanvas *>(canvas);
        SkRect dst = SkRect::MakeXYWH(x, y, width, height);
        SkPaint paint;
        paint.setAntiAlias(true);
        SkSamplingOptions sampling(SkFilterMode::kLinear, SkMipmapMode::kLinear);
        skCanvas->drawImageRect(image, dst, sampling, &paint);
        return true;
    }

    bool skia_surface_encode_png_to_file(SkiaSurface *surface, const char *filename)
    {
        if (!surface || !surface->surface || !filename)
        {
            return false;
        }

        sk_sp<SkImage> image = surface->surface->makeImageSnapshot();
        if (!image)
        {
            return false;
        }

        SkPngEncoder::Options opts;
        sk_sp<SkData> data = SkPngEncoder::Encode(nullptr, image.get(), opts);
        if (!data)
        {
            return false;
        }

        SkFILEWStream out(filename);
        if (!out.isValid())
        {
            return false;
        }
        return out.write(data->data(), data->size());
    }

} // extern "C"

extern "C"
{
    extern "C++"
    {
        static inline SkBlendMode toSkBlendMode(SkiaBlendMode mode)
        {
            switch (mode)
            {
            case SKIA_BLEND_MODE_SRC_OVER:
                return SkBlendMode::kSrcOver;
            case SKIA_BLEND_MODE_SRC:
                return SkBlendMode::kSrc;
            case SKIA_BLEND_MODE_DST_OVER:
                return SkBlendMode::kDstOver;
            case SKIA_BLEND_MODE_DST_IN:
                return SkBlendMode::kDstIn;
            case SKIA_BLEND_MODE_DST_OUT:
                return SkBlendMode::kDstOut;
            case SKIA_BLEND_MODE_SRC_IN:
                return SkBlendMode::kSrcIn;
            case SKIA_BLEND_MODE_SRC_OUT:
                return SkBlendMode::kSrcOut;
            case SKIA_BLEND_MODE_CLEAR:
                return SkBlendMode::kClear;
            case SKIA_BLEND_MODE_PLUS:
                return SkBlendMode::kPlus;
            case SKIA_BLEND_MODE_MULTIPLY:
                return SkBlendMode::kMultiply;
            case SKIA_BLEND_MODE_SCREEN:
                return SkBlendMode::kScreen;
            case SKIA_BLEND_MODE_OVERLAY:
                return SkBlendMode::kOverlay;
            case SKIA_BLEND_MODE_DARKEN:
                return SkBlendMode::kDarken;
            case SKIA_BLEND_MODE_LIGHTEN:
                return SkBlendMode::kLighten;
            case SKIA_BLEND_MODE_COLOR_DODGE:
                return SkBlendMode::kColorDodge;
            case SKIA_BLEND_MODE_COLOR_BURN:
                return SkBlendMode::kColorBurn;
            case SKIA_BLEND_MODE_HARD_LIGHT:
                return SkBlendMode::kHardLight;
            case SKIA_BLEND_MODE_SOFT_LIGHT:
                return SkBlendMode::kSoftLight;
            case SKIA_BLEND_MODE_DIFFERENCE:
                return SkBlendMode::kDifference;
            case SKIA_BLEND_MODE_EXCLUSION:
                return SkBlendMode::kExclusion;
            case SKIA_BLEND_MODE_HUE:
                return SkBlendMode::kHue;
            case SKIA_BLEND_MODE_SATURATION:
                return SkBlendMode::kSaturation;
            case SKIA_BLEND_MODE_COLOR:
                return SkBlendMode::kColor;
            case SKIA_BLEND_MODE_LUMINOSITY:
                return SkBlendMode::kLuminosity;
            default:
                return SkBlendMode::kSrcOver;
            }
        }
    }

    void skia_paint_set_blend_mode(SkiaPaint *paint, SkiaBlendMode mode)
    {
        if (!paint)
            return;
        if ((int)mode < 0 || (int)mode > (int)SKIA_BLEND_MODE_LUMINOSITY)
            return;
        reinterpret_cast<SkPaint *>(paint)->setBlendMode(toSkBlendMode(mode));
    }
} // extern "C"

#include "effects/SkImageFilters.h"
#include "core/SkTileMode.h"
#include "core/SkColor.h"

extern "C"
{
    void skia_paint_set_blur_filter(SkiaPaint *paint, float sigma_x, float sigma_y)
    {
        if (!paint)
            return;
        if (sigma_x < 0.0f)
            sigma_x = 0.0f;
        if (sigma_y < 0.0f)
            sigma_y = 0.0f;
        sk_sp<SkImageFilter> filter =
            SkImageFilters::Blur(sigma_x, sigma_y, SkTileMode::kDecal, nullptr);
        reinterpret_cast<SkPaint *>(paint)->setImageFilter(std::move(filter));
    }

    void skia_paint_set_drop_shadow_filter(SkiaPaint *paint,
                                           float dx, float dy,
                                           float sigma_x, float sigma_y,
                                           uint32_t color_argb)
    {
        if (!paint)
            return;
        if (sigma_x < 0.0f)
            sigma_x = 0.0f;
        if (sigma_y < 0.0f)
            sigma_y = 0.0f;
        sk_sp<SkImageFilter> filter =
            SkImageFilters::DropShadow(dx, dy, sigma_x, sigma_y,
                                       (SkColor)color_argb, nullptr);
        reinterpret_cast<SkPaint *>(paint)->setImageFilter(std::move(filter));
    }

    void skia_paint_set_drop_shadow_only_filter(SkiaPaint *paint,
                                                float dx, float dy,
                                                float sigma_x, float sigma_y,
                                                uint32_t color_argb)
    {
        if (!paint)
            return;
        if (sigma_x < 0.0f)
            sigma_x = 0.0f;
        if (sigma_y < 0.0f)
            sigma_y = 0.0f;
        sk_sp<SkImageFilter> filter =
            SkImageFilters::DropShadowOnly(dx, dy, sigma_x, sigma_y,
                                           (SkColor)color_argb, nullptr);
        reinterpret_cast<SkPaint *>(paint)->setImageFilter(std::move(filter));
    }

    void skia_paint_clear_image_filter(SkiaPaint *paint)
    {
        if (!paint)
            return;
        reinterpret_cast<SkPaint *>(paint)->setImageFilter(nullptr);
    }
} // extern "C"

#include "core/SkColorFilter.h"

extern "C"
{
    void skia_paint_set_color_matrix_filter(SkiaPaint *paint, const float m[20])
    {
        if (!paint || !m)
            return;
        sk_sp<SkColorFilter> filter = SkColorFilters::Matrix(m);
        reinterpret_cast<SkPaint *>(paint)->setColorFilter(std::move(filter));
    }

    void skia_paint_set_blend_color_filter(SkiaPaint *paint,
                                           uint32_t color_argb,
                                           SkiaBlendMode mode)
    {
        if (!paint)
            return;
        sk_sp<SkColorFilter> filter =
            SkColorFilters::Blend((SkColor)color_argb, toSkBlendMode(mode));
        reinterpret_cast<SkPaint *>(paint)->setColorFilter(std::move(filter));
    }

    void skia_paint_clear_color_filter(SkiaPaint *paint)
    {
        if (!paint)
            return;
        reinterpret_cast<SkPaint *>(paint)->setColorFilter(nullptr);
    }
} // extern "C"

#include "effects/SkGradient.h"

#include "core/SkFontMetrics.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

/* ============================================
 * Layers, gradients, and paragraphs
 * ============================================ */

static bool make_gradient(const uint32_t *colors, const float *stops, int count,
                          std::vector<SkColor4f> &out_colors, std::vector<float> &out_stops,
                          SkGradient &out_gradient)
{
    if (!colors || count < 2 || count > SKIA_GRADIENT_MAX_STOPS)
        return false;
    out_colors.resize((size_t)count);
    for (int i = 0; i < count; i++)
        out_colors[(size_t)i] = SkColor4f::FromColor((SkColor)colors[i]);
    if (stops)
    {
        out_stops.resize((size_t)count);
        float previous = 0.0f;
        for (int i = 0; i < count; i++)
        {
            float stop = stops[i];
            if (!(stop >= 0.0f))
                stop = 0.0f;
            if (stop > 1.0f)
                stop = 1.0f;
            if (stop < previous)
                stop = previous;
            out_stops[(size_t)i] = previous = stop;
        }
    }
    SkGradient::Interpolation interpolation;
    interpolation.fInPremul = SkGradient::Interpolation::InPremul::kYes;
    interpolation.fColorSpace = SkGradient::Interpolation::ColorSpace::kOKLab;
    out_gradient = SkGradient(
        SkGradient::Colors(SkSpan<const SkColor4f>(out_colors.data(), out_colors.size()),
                           SkSpan<const float>(out_stops.data(), out_stops.size()),
                           SkTileMode::kClamp),
        interpolation);
    return true;
}

static bool apply_shader(SkiaPaint *paint, sk_sp<SkShader> shader)
{
    if (!shader)
        return false;
    SkPaint *sk_paint = reinterpret_cast<SkPaint *>(paint);
    sk_paint->setShader(std::move(shader));
    sk_paint->setAlpha(255);
    return true;
}

static SkFont make_text_font(SkiaFont *font, float font_size)
{
    sk_sp<SkTypeface> typeface = font && font->typeface ? font->typeface : ensure_default_typeface();
    SkFont sk_font(typeface, font_size);
    sk_font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    sk_font.setSubpixel(true);
    sk_font.setHinting(SkFontHinting::kSlight);
    return sk_font;
}

/* Decode one UTF-8 code point at `text[*index]`; invalid bytes decode as
 * U+FFFD and advance by one byte. */
static SkUnichar next_utf8(const char *text, size_t length, size_t *index)
{
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(text);
    size_t i = *index;
    unsigned char lead = bytes[i];
    int extra = lead < 0x80 ? 0 : (lead >> 5) == 0x6 ? 1 : (lead >> 4) == 0xE ? 2 : (lead >> 3) == 0x1E ? 3 : -1;
    *index = i + 1;
    if (extra == 0)
        return lead;
    if (extra < 0 || i + (size_t)extra >= length)
        return 0xFFFD;
    SkUnichar value = lead & (0x3F >> extra);
    for (int k = 1; k <= extra; k++)
    {
        unsigned char next = bytes[i + (size_t)k];
        if ((next & 0xC0) != 0x80)
            return 0xFFFD;
        value = (value << 6) | (next & 0x3F);
    }
    *index = i + 1 + (size_t)extra;
    return value;
}

struct TextChar
{
    SkUnichar c;
    float advance;
    int span;
    size_t offset; /* byte offset in the span's text */
    size_t length; /* UTF-8 byte length */
};

struct SpanStyle
{
    SkFont font;
    SkFontMetrics metrics;
    float size;
};

struct ParagraphLine
{
    size_t start; /* character indices */
    size_t end;
    float width;
    bool ellipsis;
    int ellipsis_span;
    float ellipsis_width;
    float height;
    float ascent;  /* most negative ascent in the line */
    float descent; /* largest descent in the line */
};

struct ParagraphLayout
{
    std::vector<TextChar> chars;
    std::vector<SpanStyle> styles;
    std::vector<ParagraphLine> lines;
    float width = 0.0f;
    float height = 0.0f;
};

static const char *ellipsis_for(const SkFont &font)
{
    return font.unicharToGlyph(0x2026) != 0 ? "\xE2\x80\xA6" : "...";
}

static float ellipsis_width(const SkFont &font)
{
    const char *ellipsis = ellipsis_for(font);
    return font.measureText(ellipsis, strlen(ellipsis), SkTextEncoding::kUTF8);
}

/* Greedy line breaking over styled spans. Code-point advances add up to
 * SkFont::measureText (no shaping), so lines are measured from per-character
 * widths in each span's own font. */
static void layout_rich_text(const SkiaTextSpan *spans, int span_count, float max_width, float line_height,
                             int max_lines, ParagraphLayout &layout)
{
    std::vector<SkUnichar> characters;
    std::vector<SkGlyphID> glyphs;
    std::vector<SkScalar> advances;
    for (int s = 0; s < span_count; s++)
    {
        float size = spans[s].size > 0.0f ? spans[s].size : (float)BUDO_DEFAULT_FONT_SIZE;
        SpanStyle style{make_text_font(spans[s].font, size), {}, size};
        style.font.getMetrics(&style.metrics);
        layout.styles.push_back(style);
        const char *text = spans[s].text ? spans[s].text : "";
        size_t length = strlen(text);
        characters.clear();
        size_t first = layout.chars.size();
        for (size_t i = 0; i < length;)
        {
            size_t offset = i;
            SkUnichar c = next_utf8(text, length, &i);
            characters.push_back(c);
            layout.chars.push_back({c, 0.0f, s, offset, i - offset});
        }
        size_t count = characters.size();
        glyphs.resize(count);
        advances.resize(count);
        if (count)
        {
            style.font.unicharsToGlyphs(SkSpan<const SkUnichar>(characters.data(), count),
                                        SkSpan<SkGlyphID>(glyphs.data(), count));
            style.font.getWidths(SkSpan<const SkGlyphID>(glyphs.data(), count), SkSpan<SkScalar>(advances.data(), count));
        }
        for (size_t i = 0; i < count; i++)
            layout.chars[first + i].advance = characters[i] == '\n' ? 0.0f : advances[i];
    }

    std::vector<TextChar> &chars = layout.chars;
    size_t count = chars.size();
    auto is_space = [&](size_t i) { return chars[i].c == ' ' || chars[i].c == '\t'; };
    auto push = [&](size_t start, size_t end) {
        float width = 0.0f;
        while (end > start && is_space(end - 1))
            end--;
        for (size_t i = start; i < end; i++)
            width += chars[i].advance;
        layout.lines.push_back({start, end, width, false, 0, 0.0f, 0.0f, 0.0f, 0.0f});
    };

    bool wrap = max_width > 0.0f;
    size_t start = 0;
    while (count > 0 && start <= count)
    {
        size_t i = start;
        size_t last_break = SIZE_MAX;
        float width = 0.0f;
        bool hard_break = false;
        for (; i < count; i++)
        {
            if (chars[i].c == '\n')
            {
                hard_break = true;
                break;
            }
            if (is_space(i))
                last_break = i;
            else if (wrap && width + chars[i].advance > max_width && i > start)
                break;
            width += chars[i].advance;
        }
        if (i >= count || hard_break)
        {
            push(start, i);
            start = i + 1;
            if (!hard_break)
                break;
        }
        else
        {
            size_t end = last_break != SIZE_MAX && last_break > start ? last_break : i;
            push(start, end);
            start = end;
            while (start < count && is_space(start))
                start++;
        }
        if (max_lines > 0 && (int)layout.lines.size() >= max_lines)
        {
            if (start < count || hard_break)
            {
                ParagraphLine &last = layout.lines.back();
                last.ellipsis = true;
                last.ellipsis_span = last.end > last.start ? chars[last.end - 1].span
                                     : last.start < count ? chars[last.start].span
                                                          : 0;
                last.ellipsis_width = ellipsis_width(layout.styles[(size_t)last.ellipsis_span].font);
                float limit = wrap ? max_width - last.ellipsis_width : last.width;
                while (last.end > last.start && last.width > limit)
                    last.width -= chars[--last.end].advance;
                while (last.end > last.start && is_space(last.end - 1))
                    last.width -= chars[--last.end].advance;
                last.width += last.ellipsis_width;
            }
            break;
        }
    }

    /* Each line is as tall as its largest span; empty lines take the style
     * of the character that ends them. */
    for (ParagraphLine &line : layout.lines)
    {
        bool any = false;
        auto include = [&](int span) {
            const SpanStyle &style = layout.styles[(size_t)span];
            line.height = std::max(line.height, style.size * line_height);
            line.ascent = any ? std::min(line.ascent, style.metrics.fAscent) : style.metrics.fAscent;
            line.descent = any ? std::max(line.descent, style.metrics.fDescent) : style.metrics.fDescent;
            any = true;
        };
        for (size_t i = line.start; i < line.end; i++)
            include(chars[i].span);
        if (line.ellipsis)
            include(line.ellipsis_span);
        if (!any && span_count > 0)
            include(line.start < count ? chars[line.start].span : chars.empty() ? 0 : chars.back().span);
        layout.width = std::max(layout.width, line.width);
        layout.height += line.height;
    }
}

extern "C"
{
    void skia_canvas_save_layer(SkiaCanvas *canvas, const SkiaRect *bounds, uint8_t alpha,
                                float backdrop_sigma)
    {
        if (!canvas)
            return;
        SkRect rect;
        if (bounds)
            rect = toSkRect(bounds->left, bounds->top, bounds->right, bounds->bottom);
        SkPaint layer_paint;
        layer_paint.setAlpha(alpha);
        sk_sp<SkImageFilter> backdrop;
        if (backdrop_sigma > 0.0f)
            backdrop = SkImageFilters::Blur(backdrop_sigma, backdrop_sigma, SkTileMode::kClamp, nullptr);
        SkCanvas *sk_canvas = reinterpret_cast<SkCanvas *>(canvas);
        if (bounds)
        {
            sk_canvas->save();
            sk_canvas->clipRect(rect, true);
            push_layer_clip(sk_canvas, sk_canvas->getSaveCount());
        }
        SkCanvas::SaveLayerRec rec(bounds ? &rect : nullptr, alpha < 255 ? &layer_paint : nullptr,
                                   backdrop.get(), 0);
        sk_canvas->saveLayer(rec);
    }

    bool skia_paint_set_linear_gradient(SkiaPaint *paint, float x0, float y0, float x1, float y1,
                                        const uint32_t *colors, const float *stops, int count)
    {
        std::vector<SkColor4f> sk_colors;
        std::vector<float> sk_stops;
        SkGradient gradient;
        if (!paint || !make_gradient(colors, stops, count, sk_colors, sk_stops, gradient))
            return false;
        SkPoint points[2] = {{x0, y0}, {x1, y1}};
        return apply_shader(paint, SkShaders::LinearGradient(points, gradient));
    }

    bool skia_paint_set_radial_gradient(SkiaPaint *paint, float cx, float cy, float radius,
                                        const uint32_t *colors, const float *stops, int count)
    {
        std::vector<SkColor4f> sk_colors;
        std::vector<float> sk_stops;
        SkGradient gradient;
        if (!paint || !(radius > 0.0f) || !make_gradient(colors, stops, count, sk_colors, sk_stops, gradient))
            return false;
        return apply_shader(paint, SkShaders::RadialGradient({cx, cy}, radius, gradient));
    }

    bool skia_paint_set_sweep_gradient(SkiaPaint *paint, float cx, float cy,
                                       const uint32_t *colors, const float *stops, int count)
    {
        std::vector<SkColor4f> sk_colors;
        std::vector<float> sk_stops;
        SkGradient gradient;
        if (!paint || !make_gradient(colors, stops, count, sk_colors, sk_stops, gradient))
            return false;
        return apply_shader(paint, SkShaders::SweepGradient({cx, cy}, gradient));
    }

    void skia_paint_clear_shader(SkiaPaint *paint)
    {
        if (paint)
            reinterpret_cast<SkPaint *>(paint)->setShader(nullptr);
    }

    SkiaParagraphMetrics skia_measure_rich_text(const SkiaTextSpan *spans, int count, float max_width,
                                                float line_height, int max_lines)
    {
        return skia_canvas_draw_rich_text(nullptr, spans, count, 0.0f, 0.0f, max_width, line_height,
                                          SKIA_TEXT_ALIGN_LEFT, max_lines, nullptr);
    }

    SkiaParagraphMetrics skia_canvas_draw_rich_text(SkiaCanvas *canvas, const SkiaTextSpan *spans, int count,
                                                    float x, float y, float max_width, float line_height,
                                                    SkiaTextAlign align, int max_lines, SkiaPaint *paint)
    {
        SkiaParagraphMetrics metrics = {0.0f, 0.0f, 0};
        if (!spans || count <= 0)
            return metrics;
        if (!(line_height > 0.0f))
            line_height = SKIA_DEFAULT_LINE_HEIGHT;
        ParagraphLayout layout;
        layout_rich_text(spans, count, max_width, line_height, max_lines, layout);
        metrics.width = layout.width;
        metrics.height = layout.height;
        metrics.lines = (int)layout.lines.size();
        if (!canvas || !paint)
            return metrics;

        SkCanvas *sk_canvas = reinterpret_cast<SkCanvas *>(canvas);
        const SkPaint &base = *reinterpret_cast<SkPaint *>(paint);
        float box = max_width > 0.0f ? max_width : layout.width;
        float top = y;
        std::string run;
        auto draw_run = [&](int span, const std::string &text, float run_x, float baseline) {
            if (text.empty())
                return;
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(text.c_str(), layout.styles[(size_t)span].font);
            if (!blob)
                return;
            if (!spans[span].has_color)
            {
                sk_canvas->drawTextBlob(blob, run_x, baseline, base);
                return;
            }
            SkPaint styled = base;
            styled.setShader(nullptr);
            styled.setColor(toSkColor(spans[span].color));
            sk_canvas->drawTextBlob(blob, run_x, baseline, styled);
        };
        for (const ParagraphLine &line : layout.lines)
        {
            float baseline = top + (line.height - (line.descent - line.ascent)) * 0.5f - line.ascent;
            float pen = x;
            if (align == SKIA_TEXT_ALIGN_CENTER)
                pen += (box - line.width) * 0.5f;
            else if (align == SKIA_TEXT_ALIGN_RIGHT)
                pen += box - line.width;
            size_t i = line.start;
            while (i < line.end)
            {
                int span = layout.chars[i].span;
                float run_x = pen;
                run.clear();
                for (; i < line.end && layout.chars[i].span == span; i++)
                {
                    const TextChar &c = layout.chars[i];
                    run.append(spans[span].text + c.offset, c.length);
                    pen += c.advance;
                }
                draw_run(span, run, run_x, baseline);
            }
            if (line.ellipsis)
                draw_run(line.ellipsis_span, ellipsis_for(layout.styles[(size_t)line.ellipsis_span].font), pen, baseline);
            top += line.height;
        }
        return metrics;
    }

    SkiaParagraphMetrics skia_measure_paragraph(const char *text, float max_width, float font_size,
                                                float line_height, int max_lines, SkiaFont *font)
    {
        return skia_canvas_draw_paragraph(nullptr, text, 0.0f, 0.0f, max_width, font_size, line_height,
                                          SKIA_TEXT_ALIGN_LEFT, max_lines, nullptr, font);
    }

    SkiaParagraphMetrics skia_canvas_draw_paragraph(SkiaCanvas *canvas, const char *text, float x, float y,
                                                    float max_width, float font_size, float line_height,
                                                    SkiaTextAlign align, int max_lines,
                                                    SkiaPaint *paint, SkiaFont *font)
    {
        SkiaParagraphMetrics none = {0.0f, 0.0f, 0};
        if (!text || !(font_size > 0.0f))
            return none;
        SkiaTextSpan span = {text, font_size, font, 0, false};
        return skia_canvas_draw_rich_text(canvas, &span, 1, x, y, max_width, line_height, align, max_lines, paint);
    }
} // extern "C"
