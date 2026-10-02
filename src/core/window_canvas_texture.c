#include "window_canvas_texture.h"

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1

#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#endif

#include "graphics/gl_state_guard_gl.inc"

#include <stdlib.h>

struct CanvasTexture
{
    SkiaDrawingDesk drawingDesk;
    uint32_t texture_id;
    uint32_t fbo_id;
    int width;
    int height;
};

CanvasTexture *window_canvas_texture_create(int width, int height)
{
    CanvasTexture *canvas_texture;
    unsigned int texture_id = 0;
    unsigned int fbo_id = 0;
    SkiaCanvas *canvas;

    if (width <= 0 || height <= 0)
        return NULL;

    canvas_texture = (CanvasTexture *)calloc(1, sizeof(CanvasTexture));
    if (!canvas_texture)
        return NULL;

    canvas = skia_canvas_create_gl_offscreen(width, height, &texture_id, &fbo_id);
    if (!canvas)
    {
        free(canvas_texture);
        return NULL;
    }

    canvas_texture->drawingDesk.canvas = canvas;
    canvas_texture->drawingDesk.active_paint = skia_paint_create();
    canvas_texture->texture_id = texture_id;
    canvas_texture->fbo_id = fbo_id;
    canvas_texture->width = width;
    canvas_texture->height = height;
    return canvas_texture;
}

void window_canvas_texture_destroy(CanvasTexture *canvas_texture)
{
    if (!canvas_texture)
        return;

    if (canvas_texture->drawingDesk.canvas)
        skia_canvas_destroy(canvas_texture->drawingDesk.canvas);
    free(canvas_texture);
}

bool window_canvas_texture_resize(CanvasTexture *canvas_texture, int width, int height)
{
    unsigned int texture_id = 0;
    unsigned int fbo_id = 0;
    SkiaCanvas *canvas;

    if (!canvas_texture || width <= 0 || height <= 0)
        return false;
    if (canvas_texture->width == width && canvas_texture->height == height)
        return true;

    canvas = skia_canvas_create_gl_offscreen(width, height, &texture_id, &fbo_id);
    if (!canvas)
        return false;

    if (canvas_texture->drawingDesk.canvas)
        skia_canvas_destroy(canvas_texture->drawingDesk.canvas);

    canvas_texture->drawingDesk.canvas = canvas;
    canvas_texture->texture_id = texture_id;
    canvas_texture->fbo_id = fbo_id;
    canvas_texture->width = width;
    canvas_texture->height = height;
    return true;
}

SkiaCanvas *window_canvas_texture_get_canvas(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->drawingDesk.canvas : NULL;
}

SkiaDrawingDesk *window_canvas_texture_get_drawingdesk(CanvasTexture *canvas_texture)
{
    return canvas_texture ? &canvas_texture->drawingDesk : NULL;
}

uint32_t window_canvas_texture_get_gl_texture(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->texture_id : 0;
}

uint32_t window_canvas_texture_get_gl_framebuffer(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->fbo_id : 0;
}

int window_canvas_texture_get_width(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->width : 0;
}

int window_canvas_texture_get_height(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->height : 0;
}

static void flush_canvas_texture_transition(void *opaque)
{
    CanvasTexture *canvas_texture = (CanvasTexture *)opaque;

    skia_canvas_reset_gl_context(canvas_texture->drawingDesk.canvas);
    skia_canvas_flush(canvas_texture->drawingDesk.canvas);
}

void window_canvas_texture_flush(CanvasTexture *canvas_texture)
{
    if (!canvas_texture || !canvas_texture->drawingDesk.canvas)
        return;

    budo_gl_state_guard_run(flush_canvas_texture_transition, canvas_texture);
}