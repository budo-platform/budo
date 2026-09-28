#ifndef WINDOW_CANVAS_TEXTURE_H
#define WINDOW_CANVAS_TEXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/skia_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct CanvasTexture CanvasTexture;

    CanvasTexture *window_canvas_texture_create(int width, int height);
    void window_canvas_texture_destroy(CanvasTexture *canvas_texture);
    bool window_canvas_texture_resize(CanvasTexture *canvas_texture, int width, int height);
    SkiaCanvas *window_canvas_texture_get_canvas(CanvasTexture *canvas_texture);
    SkiaDrawingDesk *window_canvas_texture_get_drawingdesk(CanvasTexture *canvas_texture);
    uint32_t window_canvas_texture_get_gl_texture(CanvasTexture *canvas_texture);
    uint32_t window_canvas_texture_get_gl_framebuffer(CanvasTexture *canvas_texture);
    int window_canvas_texture_get_width(CanvasTexture *canvas_texture);
    int window_canvas_texture_get_height(CanvasTexture *canvas_texture);
    void window_canvas_texture_flush(CanvasTexture *canvas_texture);

#ifdef __cplusplus
}
#endif

#endif