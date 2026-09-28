#include "gl_state_guard.h"

#include <string.h>

static bool gl_state_guard_api_valid(const GlStateGuardApi *api)
{
    return api && api->get_integer && api->get_boolean && api->is_enabled &&
           api->set_enabled && api->bind_framebuffer && api->set_viewport &&
           api->use_program && api->bind_vertex_array && api->bind_buffer &&
           api->bind_renderbuffer && api->active_texture && api->bind_texture &&
           api->set_blend_func && api->set_blend_equation &&
           api->set_depth_func && api->set_cull_face && api->set_front_face &&
           api->set_scissor && api->set_color_mask && api->set_depth_mask;
}

bool gl_state_guard_capture(const GlStateGuardApi *api,
                            const GlStateGuardConstants *constants,
                            GlStateSnapshot *snapshot)
{
    int max_units;
    int unit;

    if (!gl_state_guard_api_valid(api) || !constants || !snapshot)
        return false;

    memset(snapshot, 0, sizeof(*snapshot));
    api->get_integer(constants->framebuffer_binding, &snapshot->framebuffer);
    api->get_integer(constants->viewport, snapshot->viewport);
    api->get_integer(constants->current_program, &snapshot->program);
    api->get_integer(constants->vertex_array_binding, &snapshot->vertex_array);
    api->get_integer(constants->array_buffer_binding, &snapshot->array_buffer);
    api->get_integer(constants->element_array_buffer_binding,
                     &snapshot->element_array_buffer);
    api->get_integer(constants->renderbuffer_binding, &snapshot->renderbuffer);
    api->get_integer(constants->active_texture_name, &snapshot->active_texture);
    api->get_integer(constants->max_texture_units, &max_units);
    if (max_units < 0)
        max_units = 0;
    if (max_units > GL_STATE_GUARD_MAX_TEXTURE_UNITS)
        max_units = GL_STATE_GUARD_MAX_TEXTURE_UNITS;
    snapshot->texture_unit_count = max_units;
    for (unit = 0; unit < max_units; unit++)
    {
        api->active_texture(constants->texture0 + (uint32_t)unit);
        api->get_integer(constants->texture_binding_2d,
                         &snapshot->texture_2d[unit]);
        api->get_integer(constants->texture_binding_cube,
                         &snapshot->texture_cube[unit]);
    }
    api->active_texture((uint32_t)snapshot->active_texture);

    snapshot->blend_enabled = api->is_enabled(constants->blend);
    snapshot->depth_test_enabled = api->is_enabled(constants->depth_test);
    snapshot->cull_face_enabled = api->is_enabled(constants->cull_face);
    snapshot->scissor_test_enabled = api->is_enabled(constants->scissor_test);
    api->get_integer(constants->blend_src_rgb, &snapshot->blend_src_rgb);
    api->get_integer(constants->blend_dst_rgb, &snapshot->blend_dst_rgb);
    api->get_integer(constants->blend_src_alpha, &snapshot->blend_src_alpha);
    api->get_integer(constants->blend_dst_alpha, &snapshot->blend_dst_alpha);
    api->get_integer(constants->blend_equation_rgb,
                     &snapshot->blend_equation_rgb);
    api->get_integer(constants->blend_equation_alpha,
                     &snapshot->blend_equation_alpha);
    api->get_integer(constants->depth_function, &snapshot->depth_function);
    api->get_integer(constants->cull_face_mode, &snapshot->cull_face_mode);
    api->get_integer(constants->front_face, &snapshot->front_face);
    api->get_integer(constants->scissor_box, snapshot->scissor_box);
    api->get_boolean(constants->color_writemask, snapshot->color_mask);
    api->get_boolean(constants->depth_writemask, &snapshot->depth_mask);
    return true;
}

static void restore_enabled(const GlStateGuardApi *api, uint32_t capability,
                            bool enabled)
{
    api->set_enabled(capability, enabled);
}

void gl_state_guard_restore(const GlStateGuardApi *api,
                            const GlStateGuardConstants *constants,
                            const GlStateSnapshot *snapshot)
{
    int unit;

    if (!gl_state_guard_api_valid(api) || !constants || !snapshot)
        return;

    api->bind_framebuffer(constants->framebuffer,
                          (uint32_t)snapshot->framebuffer);
    api->set_viewport(snapshot->viewport[0], snapshot->viewport[1],
                      snapshot->viewport[2], snapshot->viewport[3]);
    api->use_program((uint32_t)snapshot->program);
    api->bind_vertex_array((uint32_t)snapshot->vertex_array);
    api->bind_buffer(constants->array_buffer,
                     (uint32_t)snapshot->array_buffer);
    api->bind_buffer(constants->element_array_buffer,
                     (uint32_t)snapshot->element_array_buffer);
    api->bind_renderbuffer(constants->renderbuffer,
                           (uint32_t)snapshot->renderbuffer);

    for (unit = 0; unit < snapshot->texture_unit_count; unit++)
    {
        api->active_texture(constants->texture0 + (uint32_t)unit);
        api->bind_texture(constants->texture_2d,
                          (uint32_t)snapshot->texture_2d[unit]);
        api->bind_texture(constants->texture_cube,
                          (uint32_t)snapshot->texture_cube[unit]);
    }
    api->active_texture((uint32_t)snapshot->active_texture);

    restore_enabled(api, constants->blend, snapshot->blend_enabled);
    restore_enabled(api, constants->depth_test, snapshot->depth_test_enabled);
    restore_enabled(api, constants->cull_face, snapshot->cull_face_enabled);
    restore_enabled(api, constants->scissor_test, snapshot->scissor_test_enabled);
    api->set_blend_func((uint32_t)snapshot->blend_src_rgb,
                        (uint32_t)snapshot->blend_dst_rgb,
                        (uint32_t)snapshot->blend_src_alpha,
                        (uint32_t)snapshot->blend_dst_alpha);
    api->set_blend_equation((uint32_t)snapshot->blend_equation_rgb,
                            (uint32_t)snapshot->blend_equation_alpha);
    api->set_depth_func((uint32_t)snapshot->depth_function);
    api->set_cull_face((uint32_t)snapshot->cull_face_mode);
    api->set_front_face((uint32_t)snapshot->front_face);
    api->set_scissor(snapshot->scissor_box[0], snapshot->scissor_box[1],
                     snapshot->scissor_box[2], snapshot->scissor_box[3]);
    api->set_color_mask(snapshot->color_mask[0] != 0,
                        snapshot->color_mask[1] != 0,
                        snapshot->color_mask[2] != 0,
                        snapshot->color_mask[3] != 0);
    api->set_depth_mask(snapshot->depth_mask != 0);
}

bool gl_state_guard_run(const GlStateGuardApi *api,
                        const GlStateGuardConstants *constants,
                        GlStateGuardTransition transition,
                        void *context)
{
    GlStateSnapshot snapshot;

    if (!transition || !gl_state_guard_capture(api, constants, &snapshot))
        return false;
    transition(context);
    gl_state_guard_restore(api, constants, &snapshot);
    return true;
}