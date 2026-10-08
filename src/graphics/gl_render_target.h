#ifndef BUDO_GL_RENDER_TARGET_H
#define BUDO_GL_RENDER_TARGET_H

#include "graphics/gpu_resource_state.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct GlRenderTargetApi
    {
        void (*gen_textures)(int count, uint32_t *textures);
        void (*bind_texture)(uint32_t target, uint32_t texture);
        void (*tex_parameter_i)(uint32_t target, uint32_t name, int value);
        void (*tex_image_2d)(uint32_t target, int level, int internal_format,
                             int width, int height, int border,
                             uint32_t format, uint32_t type,
                             const void *pixels);
        void (*delete_textures)(int count, const uint32_t *textures);
        void (*gen_framebuffers)(int count, uint32_t *framebuffers);
        void (*bind_framebuffer)(uint32_t target, uint32_t framebuffer);
        void (*framebuffer_texture_2d)(uint32_t target, uint32_t attachment,
                                       uint32_t texture_target,
                                       uint32_t texture, int level);
        uint32_t (*check_framebuffer_status)(uint32_t target);
        void (*delete_framebuffers)(int count, const uint32_t *framebuffers);
        void (*gen_renderbuffers)(int count, uint32_t *renderbuffers);
        void (*bind_renderbuffer)(uint32_t target, uint32_t renderbuffer);
        void (*renderbuffer_storage)(uint32_t target, uint32_t format,
                                     int width, int height);
        void (*framebuffer_renderbuffer)(uint32_t target, uint32_t attachment,
                                         uint32_t renderbuffer_target,
                                         uint32_t renderbuffer);
        void (*delete_renderbuffers)(int count, const uint32_t *renderbuffers);
    } GlRenderTargetApi;

    typedef struct GlRenderTargetConstants
    {
        uint32_t texture_2d;
        uint32_t texture_min_filter;
        uint32_t texture_mag_filter;
        uint32_t texture_wrap_s;
        uint32_t texture_wrap_t;
        int linear;
        int clamp_to_edge;
        int rgba_internal;
        uint32_t rgba;
        uint32_t unsigned_byte;
        uint32_t framebuffer;
        uint32_t color_attachment0;
        uint32_t renderbuffer;
        uint32_t depth_format; 
        uint32_t depth_attachment;
        uint32_t framebuffer_complete;
    } GlRenderTargetConstants;

    bool gl_render_target_create(const GlRenderTargetApi *api,
                                 const GlRenderTargetConstants *constants,
                                 GpuRenderTargetSlot *slot,
                                 int width, int height, bool depth);
    void gl_render_target_destroy(const GlRenderTargetApi *api,
                                  GpuRenderTargetSlot *slot);
    bool gl_render_target_resize(const GlRenderTargetApi *api,
                                 const GlRenderTargetConstants *constants,
                                 GpuRenderTargetSlot *slot,
                                 int width, int height);

#ifdef __cplusplus
}
#endif

#endif