#include "gl_render_target.h"

#include <string.h>

bool gl_render_target_create(const GlRenderTargetApi *api,
                             const GlRenderTargetConstants *constants,
                             GpuRenderTargetSlot *slot,
                             int width, int height, bool depth)
{
    uint32_t status;
    if (!api || !constants || !slot || width <= 0 || height <= 0)
        return false;

    memset(slot, 0, sizeof(*slot));
    api->gen_textures(1, &slot->texture);
    api->bind_texture(constants->texture_2d, slot->texture);
    api->tex_parameter_i(constants->texture_2d,
                         constants->texture_min_filter, constants->linear);
    api->tex_parameter_i(constants->texture_2d,
                         constants->texture_mag_filter, constants->linear);
    api->tex_parameter_i(constants->texture_2d,
                         constants->texture_wrap_s, constants->clamp_to_edge);
    api->tex_parameter_i(constants->texture_2d,
                         constants->texture_wrap_t, constants->clamp_to_edge);
    api->tex_image_2d(constants->texture_2d, 0, constants->rgba_internal,
                      width, height, 0, constants->rgba,
                      constants->unsigned_byte, NULL);
    api->bind_texture(constants->texture_2d, 0);

    api->gen_framebuffers(1, &slot->fbo);
    api->bind_framebuffer(constants->framebuffer, slot->fbo);
    api->framebuffer_texture_2d(
        constants->framebuffer, constants->color_attachment0,
        constants->texture_2d, slot->texture, 0);

    if (depth)
    {
        api->gen_renderbuffers(1, &slot->depth_rbo);
        api->bind_renderbuffer(constants->renderbuffer, slot->depth_rbo);
        api->renderbuffer_storage(constants->renderbuffer,
                                  constants->depth_format,
                                  width, height);
        api->framebuffer_renderbuffer(
            constants->framebuffer, constants->depth_attachment,
            constants->renderbuffer, slot->depth_rbo);
        api->bind_renderbuffer(constants->renderbuffer, 0);
        slot->has_depth = true;
    }

    status = api->check_framebuffer_status(constants->framebuffer);
    api->bind_framebuffer(constants->framebuffer, 0);
    if (status != constants->framebuffer_complete)
    {
        gl_render_target_destroy(api, slot);
        return false;
    }

    slot->in_use = true;
    slot->width = width;
    slot->height = height;
    return true;
}

void gl_render_target_destroy(const GlRenderTargetApi *api,
                              GpuRenderTargetSlot *slot)
{
    if (!api || !slot)
        return;
    if (slot->depth_rbo)
        api->delete_renderbuffers(1, &slot->depth_rbo);
    if (slot->texture)
        api->delete_textures(1, &slot->texture);
    if (slot->fbo)
        api->delete_framebuffers(1, &slot->fbo);
    memset(slot, 0, sizeof(*slot));
}

bool gl_render_target_resize(const GlRenderTargetApi *api,
                             const GlRenderTargetConstants *constants,
                             GpuRenderTargetSlot *slot,
                             int width, int height)
{
    uint32_t status;
    if (!api || !constants || !slot || !slot->in_use ||
        width <= 0 || height <= 0)
        return false;

    api->bind_texture(constants->texture_2d, slot->texture);
    api->tex_image_2d(constants->texture_2d, 0, constants->rgba_internal,
                      width, height, 0, constants->rgba,
                      constants->unsigned_byte, NULL);
    api->bind_texture(constants->texture_2d, 0);

    if (slot->has_depth && slot->depth_rbo)
    {
        api->bind_renderbuffer(constants->renderbuffer, slot->depth_rbo);
        api->renderbuffer_storage(constants->renderbuffer,
                                  constants->depth_format,
                                  width, height);
        api->bind_renderbuffer(constants->renderbuffer, 0);
    }

    api->bind_framebuffer(constants->framebuffer, slot->fbo);
    status = api->check_framebuffer_status(constants->framebuffer);
    api->bind_framebuffer(constants->framebuffer, 0);
    if (status != constants->framebuffer_complete)
        return false;

    slot->width = width;
    slot->height = height;
    slot->depth_frame = 0; 
    return true;
}